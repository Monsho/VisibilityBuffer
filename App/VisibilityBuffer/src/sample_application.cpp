#include "sample_application.h"

#include "sl12/resource_mesh.h"
#include "sl12/string_util.h"
#include "sl12/root_signature.h"
#include "sl12/descriptor_set.h"
#include "sl12/resource_texture.h"
#include "sl12/command_queue.h"

#define NOMINMAX
#include <windowsx.h>
#include <memory>
#include <random>

#define USE_IN_CPP
#include "../shaders/cbuffer.hlsli"
#include "sl12/resource_streaming_texture.h"

namespace
{
	static const float kFovY = 90.0f;
	
	static const char* kResourceDir = "resources";
	static const char* kShaderDir = "VisibilityBuffer/shaders";
	static const char* kShaderIncludeDir = "../SampleLib12/SampleLib12/shaders/include";
	static const char* kShaderPDBDir = "ShaderPDB/";

	static const sl12::u32 kShadowMapSize = 1024;

	static const sl12::u32 kIndirectArgsBufferStride = 4 + sizeof(D3D12_DRAW_INDEXED_ARGUMENTS); // root constant + draw indexed args.

	struct MeshletBound
	{
		DirectX::XMFLOAT3		aabbMin;
		DirectX::XMFLOAT3		aabbMax;
		DirectX::XMFLOAT3		coneApex;
		DirectX::XMFLOAT3		coneAxis;
		float					coneCutoff;
		sl12::u32				pad[3];
	};	// struct MeshletBound

	static std::vector<sl12::RenderGraphTargetDesc> gGBufferDescs;
	static sl12::RenderGraphTargetDesc gAccumDesc;
	static sl12::RenderGraphTargetDesc gVisibilityDesc;
	static sl12::RenderGraphTargetDesc gMatDepthDesc;
	static sl12::RenderGraphTargetDesc gShadowExpDesc;
	static sl12::RenderGraphTargetDesc gShadowDepthDesc;
	static sl12::RenderGraphTargetDesc gAoDesc;
	static sl12::RenderGraphTargetDesc gGiDesc;
	static sl12::RenderGraphTargetDesc gDiDepthDesc;
	static sl12::RenderGraphTargetDesc gDiNormalDesc;
	static sl12::RenderGraphTargetDesc gMiplevelDesc;
	void SetGBufferDesc(sl12::u32 width, sl12::u32 height)
	{
		gGBufferDescs.clear();
		
		sl12::RenderGraphTargetDesc desc{};
		desc.name = "GBufferA";
		desc.width = width;
		desc.height = height;
		desc.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		desc.srvDescs.push_back(sl12::RenderGraphSRVDesc(0, 0, 0, 0));
		desc.rtvDescs.push_back(sl12::RenderGraphRTVDesc(0, 0, 0));
		gGBufferDescs.push_back(desc);

		desc.name = "GBufferB";
		desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		gGBufferDescs.push_back(desc);

		desc.name = "GBufferC";
		desc.format = DXGI_FORMAT_R10G10B10A2_UNORM;
		gGBufferDescs.push_back(desc);

		desc.name = "Depth";
		desc.format = DXGI_FORMAT_D32_FLOAT;
		desc.rtvDescs.clear();
		desc.dsvDescs.push_back(sl12::RenderGraphDSVDesc(0, 0, 0));
		desc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::DepthStencil;
		gGBufferDescs.push_back(desc);

		gAccumDesc.name = "Accum";
		gAccumDesc.width = width;
		gAccumDesc.height = height;
		gAccumDesc.format = DXGI_FORMAT_R11G11B10_FLOAT;
		gAccumDesc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess | sl12::ResourceUsage::RenderTarget;
		gAccumDesc.srvDescs.push_back(sl12::RenderGraphSRVDesc(0, 0, 0, 0));
		gAccumDesc.uavDescs.push_back(sl12::RenderGraphUAVDesc(0, 0, 0));
		gAccumDesc.rtvDescs.push_back(sl12::RenderGraphRTVDesc(0, 0, 0));

		gVisibilityDesc.name = "Visibility";
		gVisibilityDesc.width = width;
		gVisibilityDesc.height = height;
		gVisibilityDesc.format = DXGI_FORMAT_R32_UINT;
		gVisibilityDesc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::RenderTarget;
		gVisibilityDesc.srvDescs.push_back(sl12::RenderGraphSRVDesc(0, 0, 0, 0));
		gVisibilityDesc.rtvDescs.push_back(sl12::RenderGraphRTVDesc(0, 0, 0));

		gMatDepthDesc.name = "MatDepth";
		gMatDepthDesc.width = width;
		gMatDepthDesc.height = height;
		gMatDepthDesc.format = DXGI_FORMAT_D32_FLOAT;
		gMatDepthDesc.usage = sl12::ResourceUsage::DepthStencil;
		gMatDepthDesc.dsvDescs.push_back(sl12::RenderGraphDSVDesc(0, 0, 0));

		gShadowExpDesc.name = "ShadowExp";
		gShadowExpDesc.width = kShadowMapSize;
		gShadowExpDesc.height = kShadowMapSize;
		gShadowExpDesc.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		gShadowExpDesc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::RenderTarget;
		gShadowExpDesc.srvDescs.push_back(sl12::RenderGraphSRVDesc(0, 0, 0, 0));
		gShadowExpDesc.rtvDescs.push_back(sl12::RenderGraphRTVDesc(0, 0, 0));

		gShadowDepthDesc.name = "ShadowDepth";
		gShadowDepthDesc.width = kShadowMapSize;
		gShadowDepthDesc.height = kShadowMapSize;
		gShadowDepthDesc.format = DXGI_FORMAT_D32_FLOAT;
		gShadowDepthDesc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::DepthStencil;
		gShadowDepthDesc.srvDescs.push_back(sl12::RenderGraphSRVDesc(0, 0, 0, 0));
		gShadowDepthDesc.dsvDescs.push_back(sl12::RenderGraphDSVDesc(0, 0, 0));

		gAoDesc.name = "AO";
		gAoDesc.width = width;
		gAoDesc.height = height;
		gAoDesc.format = DXGI_FORMAT_R8_UNORM;
		gAoDesc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess;
		gAoDesc.srvDescs.push_back(sl12::RenderGraphSRVDesc(0, 0, 0, 0));
		gAoDesc.uavDescs.push_back(sl12::RenderGraphUAVDesc(0, 0, 0));

		gGiDesc.name = "GI";
		gGiDesc.width = width;
		gGiDesc.height = height;
		gGiDesc.format = DXGI_FORMAT_R11G11B10_FLOAT;
		gGiDesc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess;
		gGiDesc.srvDescs.push_back(sl12::RenderGraphSRVDesc(0, 0, 0, 0));
		gGiDesc.uavDescs.push_back(sl12::RenderGraphUAVDesc(0, 0, 0));

		gDiDepthDesc.name = "Depth";
		gDiDepthDesc.width = width;
		gDiDepthDesc.height = height;
		gDiDepthDesc.format = DXGI_FORMAT_R32_FLOAT;
		gDiDepthDesc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess;
		gDiDepthDesc.srvDescs.push_back(sl12::RenderGraphSRVDesc(0, 0, 0, 0));
		gDiDepthDesc.uavDescs.push_back(sl12::RenderGraphUAVDesc(0, 0, 0));

		gDiNormalDesc = gGBufferDescs[2];
		gDiNormalDesc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess;
		gDiNormalDesc.rtvDescs.clear();
		gDiNormalDesc.srvDescs.push_back(sl12::RenderGraphSRVDesc(0, 0, 0, 0));
		gDiNormalDesc.uavDescs.push_back(sl12::RenderGraphUAVDesc(0, 0, 0));

		gMiplevelDesc.name = "Miplevel";
		gMiplevelDesc.width = (width + 3) / 4;
		gMiplevelDesc.height = (height + 3) / 4;
		gMiplevelDesc.format = DXGI_FORMAT_R8G8_UINT;
		gMiplevelDesc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess;
		gMiplevelDesc.srvDescs.push_back(sl12::RenderGraphSRVDesc(0, 0, 0, 0));
		gMiplevelDesc.uavDescs.push_back(sl12::RenderGraphUAVDesc(0, 0, 0));
	}

	enum ShaderName
	{
		DepthVV,
		MeshVV,
		MeshP,
		VisibilityVV,
		VisibilityP,
		LightingC,
		IndirectC,
		FullscreenVV,
		TonemapP,
		ClassifyC,
		MatDepthP,
		ClearArgC,
		MaterialTileVV,
		MaterialTileP,
		ShadowVV,
		ShadowP,
		BlurP,
		NormalToDerivC,
		TriplanarVV,
		TriplanarP,
		MaterialTileTriplanarP,
		SsaoHbaoC,
		SsaoBitmaskC,
		SsgiC,
		SsgiDIC,
		DenoiseC,
		DenoiseWithGIC,
		DeinterleaveC,
		MeshletCullC,
		ClearMipC,
		FeedbackMipC,

		MAX
	};
	static const char* kShaderFileAndEntry[] = {
		"depth.vv.hlsl",					"main",
		"mesh.vv.hlsl",						"main",
		"mesh.p.hlsl",						"main",
		"visibility.vv.hlsl",				"main",
		"visibility.p.hlsl",				"main",
		"lighting.c.hlsl",					"main",
		"indirect_lighting.c.hlsl",			"main",
		"fullscreen.vv.hlsl",				"main",
		"tonemap.p.hlsl",					"main",
		"classify.c.hlsl",					"main",
		"material_depth.p.hlsl",			"main",
		"clear_arg.c.hlsl",					"main",
		"material_tile.vv.hlsl",			"main",
		"material_tile.p.hlsl",				"main",
		"shadow.vv.hlsl",					"main",
		"shadow.p.hlsl",					"main",
		"blur.p.hlsl",						"main",
		"ts_normal_to_deriv.c.hlsl",		"main",
		"triplanar.vv.hlsl",				"main",
		"triplanar.p.hlsl",					"main",
		"material_tile_triplanar.p.hlsl",	"main",
		"ssao_hbao.c.hlsl",					"main",
		"ssao_bitmask.c.hlsl",				"main",
		"ssgi_standard.c.hlsl",				"main",
		"ssgi_deinterleave.c.hlsl",			"main",
		"denoise.c.hlsl",					"main",
		"denoise_with_gi.c.hlsl",			"main",
		"deinterleave.c.hlsl",				"main",
		"meshlet_cull.c.hlsl",				"main",
		"miplevel_feedback.c.hlsl",			"ClearCS",
		"miplevel_feedback.c.hlsl",			"FeedbackCS",
	};

	sl12::TextureView* GetTextureView(sl12::ResourceHandle resTexHandle, sl12::TextureView* pDummyView)
	{
		auto resTex = resTexHandle.GetItem<sl12::ResourceItemTextureBase>();
		if (resTex)
		{
			return &const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView();
		}
		return pDummyView;
	}
}

SampleApplication::SampleApplication(HINSTANCE hInstance, int nCmdShow, int screenWidth, int screenHeight, sl12::ColorSpaceType csType, const std::string& homeDir, int meshType)
	: Application(hInstance, nCmdShow, screenWidth, screenHeight, csType)
	, displayWidth_(screenWidth), displayHeight_(screenHeight)
	, meshType_(meshType)
{
	std::filesystem::path p(homeDir);
	p = std::filesystem::absolute(p);
	homeDir_ = p.string();
}

SampleApplication::~SampleApplication()
{}

bool SampleApplication::Initialize()
{
	// initialize mesh manager.
	const size_t kVertexBufferSize = 512 * 1024 * 1024;		// 512MB
	const size_t kIndexBufferSize = 64 * 1024 * 1024;		// 64MB
	meshMan_ = sl12::MakeUnique<sl12::MeshManager>(&device_, &device_, kVertexBufferSize, kIndexBufferSize);
	
	// initialize resource loader.
	resLoader_ = sl12::MakeUnique<sl12::ResourceLoader>(nullptr);
	if (!resLoader_->Initialize(&device_, &meshMan_, sl12::JoinPath(homeDir_, kResourceDir)))
	{
		sl12::ConsolePrint("Error: failed to init resource loader.");
		return false;
	}

	// initialize texture streamer.
	texStreamer_ = sl12::MakeUnique<sl12::TextureStreamer>(&device_);
	if (!texStreamer_->Initialize(&device_))
	{
		sl12::ConsolePrint("Error: failed to init texture streamer.");
		return false;
	}

	// initialize shader manager.
	std::vector<std::string> shaderIncludeDirs;
	shaderIncludeDirs.push_back(sl12::JoinPath(homeDir_, kShaderIncludeDir));
	shaderMan_ = sl12::MakeUnique<sl12::ShaderManager>(nullptr);
	sl12::ShaderPDB::Type pdbType = sl12::ShaderPDB::None;
	std::string pdbDir = sl12::JoinPath(homeDir_, kShaderPDBDir);
#if 0 // if 1, output shader debug files
	pdbType = sl12::ShaderPDB::Full;
#endif
	if (!shaderMan_->Initialize(&device_, &shaderIncludeDirs, pdbType, &pdbDir))
	{
		sl12::ConsolePrint("Error: failed to init shader manager.");
		return false;
	}

	// compile shaders.
	const std::string shaderBaseDir = sl12::JoinPath(homeDir_, kShaderDir);
	for (int i = 0; i < ShaderName::MAX; i++)
	{
		const char* file = kShaderFileAndEntry[i * 2 + 0];
		const char* entry = kShaderFileAndEntry[i * 2 + 1];
		auto handle = shaderMan_->CompileFromFile(
			sl12::JoinPath(shaderBaseDir, file),
			entry, sl12::GetShaderTypeFromFileName(file), 6, 5, nullptr, nullptr);
		hShaders_.push_back(handle);
	}
	
	// load request.
	if (meshType_ == 0)
	{
		hSuzanneMesh_ = resLoader_->LoadRequest<sl12::ResourceItemMesh>("mesh/hp_suzanne/hp_suzanne.rmesh");
	}
	else if (meshType_ == 1)
	{
		//hSponzaMesh_ = resLoader_->LoadRequest<sl12::ResourceItemMesh>("mesh/IntelSponza/IntelSponza.rmesh");
		hSponzaMesh_ = resLoader_->LoadRequest<sl12::ResourceItemMesh>("mesh/sponza/sponza.rmesh");
		hSphereMesh_ = resLoader_->LoadRequest<sl12::ResourceItemMesh>("mesh/sphere/sphere.rmesh");
	}
	else if (meshType_ == 2)
	{
		hSponzaMesh_ = resLoader_->LoadRequest<sl12::ResourceItemMesh>("mesh/IntelSponza/IntelSponza.rmesh");
		hCurtainMesh_ = resLoader_->LoadRequest<sl12::ResourceItemMesh>("mesh/IntelCurtain/IntelCurtain.rmesh");
	}
	hDetailTex_ = resLoader_->LoadRequest<sl12::ResourceItemTexture>("texture/detail_normal.dds");
	hDotTex_ = resLoader_->LoadRequest<sl12::ResourceItemTexture>("texture/dot_normal.dds");

	// init command list.
	mainCmdList_ = sl12::MakeUnique<CommandLists>(nullptr);
	if (!mainCmdList_->Initialize(&device_, &device_.GetGraphicsQueue()))
	{
		sl12::ConsolePrint("Error: failed to init main command list.");
		return false;
	}

	// init cbv manager.
	cbvMan_ = sl12::MakeUnique<sl12::CbvManager>(nullptr, &device_);

	// init render graph.
	renderGraph_ = sl12::MakeUnique<sl12::RenderGraph>(nullptr);

	// get GBuffer target descs.
	SetGBufferDesc(displayWidth_, displayHeight_);
	
	// create sampler.
	{
		linearSampler_ = sl12::MakeUnique<sl12::Sampler>(&device_);
		linearClampSampler_ = sl12::MakeUnique<sl12::Sampler>(&device_);
		shadowSampler_ = sl12::MakeUnique<sl12::Sampler>(&device_);

		D3D12_SAMPLER_DESC desc{};
		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.MaxLOD = FLT_MAX;
		desc.MinLOD = 0.0f;
		desc.MipLODBias = 0.0f;
		linearSampler_->Initialize(&device_, desc);

		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		linearClampSampler_->Initialize(&device_, desc);

		desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER;
		shadowSampler_->Initialize(&device_, desc);
	}
	
	// wait compile and load.
	while (shaderMan_->IsCompiling() || resLoader_->IsLoading())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	
	// init utility command list.
	auto utilCmdList = sl12::MakeUnique<sl12::CommandList>(&device_);
	utilCmdList->Initialize(&device_, &device_.GetGraphicsQueue());
	utilCmdList->Reset();

	// init GUI.
	gui_ = sl12::MakeUnique<sl12::Gui>(nullptr);
	if (!gui_->Initialize(&device_, device_.GetSwapchain().GetTexture(0)->GetResourceDesc().Format))
	{
		sl12::ConsolePrint("Error: failed to init GUI.");
		return false;
	}
	if (!gui_->CreateFontImage(&device_, &utilCmdList))
	{
		sl12::ConsolePrint("Error: failed to create GUI font.");
		return false;
	}

	// create dummy texture.
	if (!device_.CreateDummyTextures(&utilCmdList))
	{
		return false;
	}

	// create meshlet bounds buffers.
	CreateMeshletBounds(&utilCmdList);
	
	// execute utility commands.
	utilCmdList->Close();
	utilCmdList->Execute();
	device_.WaitDrawDone();

	// create scene meshes.
	if (meshType_ == 0)
	{
		static const int kMeshWidth = 32;
		static const float kMeshInter = 100.0f;
		static const float kMeshOrigin = -(kMeshWidth - 1) * kMeshInter * 0.5f;
		std::random_device seed_gen;
		std::mt19937 rnd(seed_gen());
		auto RandRange = [&rnd](float minV, float maxV)
		{
			sl12::u32 val = rnd();
			float v0_1 = (float)val / (float)0xffffffff;
			return minV + (maxV - minV) * v0_1;
		};
		for (int x = 0; x < kMeshWidth; x++)
		{
			for (int y = 0; y < kMeshWidth; y++)
			{
				auto mesh = std::make_shared<sl12::SceneMesh>(&device_, hSuzanneMesh_.GetItem<sl12::ResourceItemMesh>());
				DirectX::XMFLOAT3 pos(kMeshOrigin + x * kMeshInter, RandRange(-100.0f, 100.0f), kMeshOrigin + y * kMeshInter);
				DirectX::XMFLOAT4X4 mat;
				DirectX::XMMATRIX m = DirectX::XMMatrixRotationRollPitchYaw(RandRange(-DirectX::XM_PI, DirectX::XM_PI), RandRange(-DirectX::XM_PI, DirectX::XM_PI), RandRange(-DirectX::XM_PI, DirectX::XM_PI))
										* DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
				DirectX::XMStoreFloat4x4(&mat, m);
				mesh->SetMtxLocalToWorld(mat);

				sceneMeshes_.push_back(mesh);
			}
		}
	}
	else if (meshType_ == 1)
	{
		// sponza
		{
			auto mesh = std::make_shared<sl12::SceneMesh>(&device_, hSponzaMesh_.GetItem<sl12::ResourceItemMesh>());
			DirectX::XMFLOAT3 pos(0.0f, -300.0f, 100.0f);
			DirectX::XMFLOAT3 scl(0.02f, 0.02f, 0.02f);
			DirectX::XMFLOAT4X4 mat;
			DirectX::XMMATRIX m = DirectX::XMMatrixScaling(scl.x, scl.y, scl.z)
									* DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
			DirectX::XMStoreFloat4x4(&mat, m);
			mesh->SetMtxLocalToWorld(mat);

			sceneMeshes_.push_back(mesh);
		}
		// sphere
		{
			auto mesh = std::make_shared<sl12::SceneMesh>(&device_, hSphereMesh_.GetItem<sl12::ResourceItemMesh>());
			DirectX::XMFLOAT3 pos(0.0f, 2000.0f, 0.0f);
			DirectX::XMFLOAT3 scl(200.0f, 200.0f, 200.0f);
			DirectX::XMFLOAT4X4 mat;
			DirectX::XMMATRIX m = DirectX::XMMatrixScaling(scl.x, scl.y, scl.z)
									* DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
			DirectX::XMStoreFloat4x4(&mat, m);
			mesh->SetMtxLocalToWorld(mat);

			sceneMeshes_.push_back(mesh);
		}
	}
	else if (meshType_ == 2)
	{
		// sponza
		{
			auto mesh = std::make_shared<sl12::SceneMesh>(&device_, hSponzaMesh_.GetItem<sl12::ResourceItemMesh>());
			DirectX::XMFLOAT3 pos(0.0f, 100.0f, 0.0f);
			DirectX::XMFLOAT3 scl(100.0f, 100.0f, 100.0f);
			DirectX::XMFLOAT4X4 mat;
			DirectX::XMMATRIX m = DirectX::XMMatrixScaling(scl.x, scl.y, scl.z)
									* DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
			DirectX::XMStoreFloat4x4(&mat, m);
			mesh->SetMtxLocalToWorld(mat);

			sceneMeshes_.push_back(mesh);
		}
		// curtain
		{
			auto mesh = std::make_shared<sl12::SceneMesh>(&device_, hCurtainMesh_.GetItem<sl12::ResourceItemMesh>());
			DirectX::XMFLOAT3 pos(0.0f, 100.0f, 0.0f);
			DirectX::XMFLOAT3 scl(100.0f, 100.0f, 100.0f);
			DirectX::XMFLOAT4X4 mat;
			DirectX::XMMATRIX m = DirectX::XMMatrixScaling(scl.x, scl.y, scl.z)
									* DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
			DirectX::XMStoreFloat4x4(&mat, m);
			mesh->SetMtxLocalToWorld(mat);

			sceneMeshes_.push_back(mesh);
		}
	}
	ComputeSceneAABB();
	CreateMaterialList();
	
	// init root signature and pipeline state.
	rsVsPs_ = sl12::MakeUnique<sl12::RootSignature>(&device_);
	rsVsPsC1_ = sl12::MakeUnique<sl12::RootSignature>(&device_);
	psoDepth_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoMesh_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoTriplanar_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoVisibility_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoTonemap_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoMatDepth_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoMaterialTile_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoMaterialTileTriplanar_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoShadowDepth_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoShadowExp_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoBlur_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	rsVsPs_->Initialize(&device_, hShaders_[ShaderName::MeshVV].GetShader(), hShaders_[ShaderName::MeshP].GetShader(), nullptr, nullptr, nullptr);
	rsVsPsC1_->Initialize(&device_, hShaders_[ShaderName::VisibilityVV].GetShader(), hShaders_[ShaderName::VisibilityP].GetShader(), nullptr, nullptr, nullptr, 1);
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsVsPs_;
		desc.pVS = hShaders_[ShaderName::DepthVV].GetShader();

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_BACK;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

		D3D12_INPUT_ELEMENT_DESC input_elems[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		desc.inputLayout.numElements = ARRAYSIZE(input_elems);
		desc.inputLayout.pElements = input_elems;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.dsvFormat = gGBufferDescs[3].format;
		desc.multisampleCount = 1;

		if (!psoDepth_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init depth pso.");
			return false;
		}
	}
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsVsPsC1_;
		desc.pVS = hShaders_[ShaderName::MeshVV].GetShader();
		desc.pPS = hShaders_[ShaderName::MeshP].GetShader();

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_BACK;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

		D3D12_INPUT_ELEMENT_DESC input_elems[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		desc.inputLayout.numElements = ARRAYSIZE(input_elems);
		desc.inputLayout.pElements = input_elems;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = gGBufferDescs[0].format;
		desc.rtvFormats[desc.numRTVs++] = gGBufferDescs[1].format;
		desc.rtvFormats[desc.numRTVs++] = gGBufferDescs[2].format;
		desc.dsvFormat = gGBufferDescs[3].format;
		desc.multisampleCount = 1;

		if (!psoMesh_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init mesh pso.");
			return false;
		}
	}
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsVsPsC1_;
		desc.pVS = hShaders_[ShaderName::TriplanarVV].GetShader();
		desc.pPS = hShaders_[ShaderName::TriplanarP].GetShader();

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_BACK;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

		D3D12_INPUT_ELEMENT_DESC input_elems[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		desc.inputLayout.numElements = ARRAYSIZE(input_elems);
		desc.inputLayout.pElements = input_elems;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = gGBufferDescs[0].format;
		desc.rtvFormats[desc.numRTVs++] = gGBufferDescs[1].format;
		desc.rtvFormats[desc.numRTVs++] = gGBufferDescs[2].format;
		desc.dsvFormat = gGBufferDescs[3].format;
		desc.multisampleCount = 1;

		if (!psoTriplanar_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init triplanar pso.");
			return false;
		}
	}
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsVsPsC1_;
		desc.pVS = hShaders_[ShaderName::VisibilityVV].GetShader();
		desc.pPS = hShaders_[ShaderName::VisibilityP].GetShader();

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_BACK;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

		D3D12_INPUT_ELEMENT_DESC input_elems[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		desc.inputLayout.numElements = ARRAYSIZE(input_elems);
		desc.inputLayout.pElements = input_elems;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = gVisibilityDesc.format;
		desc.dsvFormat = gGBufferDescs[3].format;
		desc.multisampleCount = 1;

		if (!psoVisibility_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init mesh pso.");
			return false;
		}
	}
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsVsPs_;
		desc.pVS = hShaders_[ShaderName::FullscreenVV].GetShader();
		desc.pPS = hShaders_[ShaderName::TonemapP].GetShader();

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
		desc.rtvFormats[desc.numRTVs++] = device_.GetSwapchain().GetTexture(0)->GetResourceDesc().Format;
		desc.dsvFormat = DXGI_FORMAT_UNKNOWN;
		desc.multisampleCount = 1;

		if (!psoTonemap_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init tonemap pso.");
			return false;
		}
	}
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsVsPs_;
		desc.pVS = hShaders_[ShaderName::FullscreenVV].GetShader();
		desc.pPS = hShaders_[ShaderName::MatDepthP].GetShader();

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
		desc.multisampleCount = 1;

		if (!psoMatDepth_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init material depth pso.");
			return false;
		}
	}
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsVsPs_;
		desc.pVS = hShaders_[ShaderName::MaterialTileVV].GetShader();
		desc.pPS = hShaders_[ShaderName::MaterialTileP].GetShader();

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_NONE;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = false;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_EQUAL;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.rtvFormats[desc.numRTVs++] = gGBufferDescs[0].format;
		desc.rtvFormats[desc.numRTVs++] = gGBufferDescs[1].format;
		desc.rtvFormats[desc.numRTVs++] = gGBufferDescs[2].format;
		desc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
		desc.multisampleCount = 1;

		if (!psoMaterialTile_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init material tile pso.");
			return false;
		}

		desc.pPS = hShaders_[ShaderName::MaterialTileTriplanarP].GetShader();

		if (!psoMaterialTileTriplanar_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init material tile triplanar pso.");
			return false;
		}
	}
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsVsPs_;
		desc.pVS = hShaders_[ShaderName::ShadowVV].GetShader();

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_FRONT;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = false;
		desc.rasterizer.isFrontCCW = true;
		desc.rasterizer.slopeScaledDepthBias = 2.0f;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

		D3D12_INPUT_ELEMENT_DESC input_elems[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};
		desc.inputLayout.numElements = ARRAYSIZE(input_elems);
		desc.inputLayout.pElements = input_elems;

		desc.primTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		desc.numRTVs = 0;
		desc.dsvFormat = gShadowDepthDesc.format;
		desc.multisampleCount = 1;

		if (!psoShadowDepth_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init shadow depth pso.");
			return false;
		}
	}
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsVsPs_;
		desc.pVS = hShaders_[ShaderName::FullscreenVV].GetShader();
		desc.pPS = hShaders_[ShaderName::ShadowP].GetShader();

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
		desc.rtvFormats[desc.numRTVs++] = gShadowExpDesc.format;
		desc.dsvFormat = DXGI_FORMAT_UNKNOWN;
		desc.multisampleCount = 1;

		if (!psoShadowExp_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init shadow exponent pso.");
			return false;
		}
	}
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsVsPs_;
		desc.pVS = hShaders_[ShaderName::FullscreenVV].GetShader();
		desc.pPS = hShaders_[ShaderName::BlurP].GetShader();

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
		desc.rtvFormats[desc.numRTVs++] = gShadowExpDesc.format;
		desc.dsvFormat = DXGI_FORMAT_UNKNOWN;
		desc.multisampleCount = 1;

		if (!psoBlur_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init blur pso.");
			return false;
		}
	}

	rsCs_ = sl12::MakeUnique<sl12::RootSignature>(&device_);
	psoLighting_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoIndirect_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoClassify_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoClearArg_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoNormalToDeriv_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoSsaoHbao_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoSsaoBitmask_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoSsgi_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoSsgiDI_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoDenoise_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoDenoiseGI_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoDeinterleave_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoMeshletCull_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoClearMip_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoFeedbackMip_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	rsCs_->Initialize(&device_, hShaders_[ShaderName::LightingC].GetShader());
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::LightingC].GetShader();

		if (!psoLighting_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init lighting pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::IndirectC].GetShader();

		if (!psoIndirect_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init indirect lighting pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::ClassifyC].GetShader();

		if (!psoClassify_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init classify pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::ClearArgC].GetShader();

		if (!psoClearArg_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init clear arg pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::NormalToDerivC].GetShader();

		if (!psoNormalToDeriv_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init normal to deriv pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::SsaoHbaoC].GetShader();

		if (!psoSsaoHbao_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init ssao hbao pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::SsaoBitmaskC].GetShader();

		if (!psoSsaoBitmask_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init ssao bitmask pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::SsgiC].GetShader();

		if (!psoSsgi_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init ssgi pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::SsgiDIC].GetShader();

		if (!psoSsgiDI_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init ssgi deinterleave pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::DenoiseC].GetShader();

		if (!psoDenoise_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init denoise pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::DenoiseWithGIC].GetShader();

		if (!psoDenoiseGI_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init denoise with gi pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::DeinterleaveC].GetShader();

		if (!psoDeinterleave_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init deinterleave pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::MeshletCullC].GetShader();

		if (!psoMeshletCull_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init meshlet cull pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::ClearMipC].GetShader();

		if (!psoClearMip_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init clear miplevel pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hShaders_[ShaderName::FeedbackMipC].GetShader();

		if (!psoFeedbackMip_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init feedback miplevel pso.");
			return false;
		}
	}

	{
		tileDrawIndirect_ = sl12::MakeUnique<sl12::IndirectExecuter>(&device_);
		if (!tileDrawIndirect_->Initialize(&device_, sl12::IndirectType::Draw, 0))
		{
			return false;
		}
	}

	for (auto&& t : timestamps_)
	{
		t.Initialize(&device_, 16);
	}

	cameraPos_ = DirectX::XMFLOAT3(1000.0f, 1000.0f, 0.0f);
	cameraDir_ = DirectX::XMFLOAT3(-1.0f, 0.0f, 0.0f);
	lastMouseX_ = lastMouseY_ = 0;
	return true;
}

void SampleApplication::Finalize()
{
	// wait render.
	device_.WaitDrawDone();
	device_.Present(1);

	// destroy render objects.
	miplevelUAV_.Reset();
	miplevelBuffer_.Reset();
	SuzanneMeshletBV_.Reset();
	SponzaMeshletBV_.Reset();
	CurtainMeshletBV_.Reset();
	SphereMeshletBV_.Reset();
	SuzanneMeshletB_.Reset();
	SponzaMeshletB_.Reset();
	CurtainMeshletB_.Reset();
	SphereMeshletB_.Reset();
	meshletCBs_.clear();
	for (auto&& t : timestamps_) t.Destroy();
	tileDrawIndirect_.Reset();
	gui_.Reset();
	psoFeedbackMip_.Reset();
	psoClearMip_.Reset();
	psoMeshletCull_.Reset();
	psoNormalToDeriv_.Reset();
	psoLighting_.Reset();
	psoIndirect_.Reset();
	psoClearArg_.Reset();
	psoClassify_.Reset();
	psoDenoise_.Reset();
	psoDenoiseGI_.Reset();
	psoDeinterleave_.Reset();
	psoSsgi_.Reset();
	psoSsgiDI_.Reset();
	psoSsaoBitmask_.Reset();
	psoSsaoHbao_.Reset();
	psoBlur_.Reset();
	psoShadowExp_.Reset();
	psoShadowDepth_.Reset();
	psoMaterialTile_.Reset();
	psoMaterialTileTriplanar_.Reset();
	psoTonemap_.Reset();
	psoMatDepth_.Reset();
	psoVisibility_.Reset();
	psoTriplanar_.Reset();
	psoMesh_.Reset();
	psoDepth_.Reset();
	rsCs_.Reset();
	rsVsPsC1_.Reset();
	rsVsPs_.Reset();
	renderGraph_.Reset();
	cbvMan_.Reset();
	mainCmdList_.Reset();
	shaderMan_.Reset();
	texStreamer_.Reset();
	resLoader_.Reset();
	meshMan_.Reset();
}

struct TargetIDContainer
{
	std::vector<sl12::RenderGraphTargetID> gbufferTargetIDs;
	sl12::RenderGraphTargetID accumTargetID;
	sl12::RenderGraphTargetID visibilityTargetID;
	sl12::RenderGraphTargetID matDepthTargetID;
	sl12::RenderGraphTargetID shadowExpTargetID, shadowExpTmpTargetID;
	sl12::RenderGraphTargetID shadowDepthTargetID;
	sl12::RenderGraphTargetID ssaoTargetID, ssgiTargetID, denoiseTargetID, denoiseGITargetID;
	sl12::RenderGraphTargetID diDepthTargetID, diNormalTargetID, diAccumTargetID;
	sl12::RenderGraphTargetID miplevelTargetID;
};
void SampleApplication::SetupRenderGraph(TargetIDContainer& OutContainer)
{
	bool bNeedDeinterleave = bIsDeinterleave_ && (ssaoType_ == 2);

	for (auto&& desc : gGBufferDescs)
	{
		OutContainer.gbufferTargetIDs.push_back(renderGraph_->AddTarget(desc));
	}
	OutContainer.accumTargetID = renderGraph_->AddTarget(gAccumDesc);
	OutContainer.visibilityTargetID = renderGraph_->AddTarget(gVisibilityDesc);
	OutContainer.matDepthTargetID = renderGraph_->AddTarget(gMatDepthDesc);
	OutContainer.shadowExpTargetID = renderGraph_->AddTarget(gShadowExpDesc);
	OutContainer.shadowExpTmpTargetID = renderGraph_->AddTarget(gShadowExpDesc);
	OutContainer.shadowDepthTargetID = renderGraph_->AddTarget(gShadowDepthDesc);
	OutContainer.ssaoTargetID = renderGraph_->AddTarget(gAoDesc);
	OutContainer.ssgiTargetID = renderGraph_->AddTarget(gGiDesc);
	OutContainer.denoiseTargetID = renderGraph_->AddTarget(gAoDesc);
	OutContainer.denoiseGITargetID = renderGraph_->AddTarget(gGiDesc);
	if (bNeedDeinterleave)
	{
		OutContainer.diDepthTargetID = renderGraph_->AddTarget(gDiDepthDesc);
		OutContainer.diNormalTargetID = renderGraph_->AddTarget(gDiNormalDesc);
		OutContainer.diAccumTargetID = renderGraph_->AddTarget(gAccumDesc);
	}
	OutContainer.miplevelTargetID = renderGraph_->AddTarget(gMiplevelDesc);

	// create render passes.
	std::vector<sl12::RenderPass> passes;
	std::vector<sl12::RenderGraphTargetID> histories;

	// shadow depth pass.
	sl12::RenderPass shadowPass{};
	shadowPass.output.push_back(OutContainer.shadowExpTargetID);
	shadowPass.output.push_back(OutContainer.shadowDepthTargetID);
	shadowPass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
	shadowPass.outputStates.push_back(D3D12_RESOURCE_STATE_DEPTH_WRITE);
	passes.push_back(shadowPass);

#if SHADOW_TYPE == 1
	// shadow exponent pass.
	sl12::RenderPass shadowExpPass{};
	shadowExpPass.input.push_back(OutContainer.shadowDepthTargetID);
	shadowExpPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	shadowExpPass.output.push_back(OutContainer.shadowExpTargetID);
	shadowExpPass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
	passes.push_back(shadowExpPass);

	if (evsmBlur_)
	{
		// shadow blur x pass.
		sl12::RenderPass shadowBlurXPass{};
		shadowBlurXPass.input.push_back(OutContainer.shadowExpTargetID);
		shadowBlurXPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
		shadowBlurXPass.output.push_back(OutContainer.shadowExpTmpTargetID);
		shadowBlurXPass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
		passes.push_back(shadowBlurXPass);

		// shadow blur y pass.
		sl12::RenderPass shadowBlurYPass{};
		shadowBlurYPass.input.push_back(OutContainer.shadowExpTmpTargetID);
		shadowBlurYPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
		shadowBlurYPass.output.push_back(OutContainer.shadowExpTargetID);
		shadowBlurYPass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
		passes.push_back(shadowBlurYPass);
	}
#endif

	// clear miplevel pass.
	sl12::RenderPass clearMipPass{};
	clearMipPass.output.push_back(OutContainer.miplevelTargetID);
	clearMipPass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	passes.push_back(clearMipPass);
	
	if (!bEnableVisibilityBuffer_)
	{
		// depth pre pass.
		sl12::RenderPass depthPass{};
		depthPass.output.push_back(OutContainer.gbufferTargetIDs[3]);
		depthPass.outputStates.push_back(D3D12_RESOURCE_STATE_DEPTH_WRITE);
		passes.push_back(depthPass);
		
		// gbuffer pass.
		sl12::RenderPass gbufferPass{};
		gbufferPass.output.push_back(OutContainer.gbufferTargetIDs[0]);
		gbufferPass.output.push_back(OutContainer.gbufferTargetIDs[1]);
		gbufferPass.output.push_back(OutContainer.gbufferTargetIDs[2]);
		gbufferPass.output.push_back(OutContainer.gbufferTargetIDs[3]);
		gbufferPass.output.push_back(OutContainer.miplevelTargetID);
		gbufferPass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
		gbufferPass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
		gbufferPass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
		gbufferPass.outputStates.push_back(D3D12_RESOURCE_STATE_DEPTH_WRITE);
		gbufferPass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		passes.push_back(gbufferPass);
	}
	else
	{
		// visibility pass.
		sl12::RenderPass visibilityPass{};
		visibilityPass.output.push_back(OutContainer.visibilityTargetID);
		visibilityPass.output.push_back(OutContainer.gbufferTargetIDs[3]);
		visibilityPass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
		visibilityPass.outputStates.push_back(D3D12_RESOURCE_STATE_DEPTH_WRITE);
		passes.push_back(visibilityPass);

		// material depth pass.
		sl12::RenderPass matDepthPass{};
		matDepthPass.input.push_back(OutContainer.visibilityTargetID);
		matDepthPass.input.push_back(OutContainer.gbufferTargetIDs[3]);
		matDepthPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
		matDepthPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
		matDepthPass.output.push_back(OutContainer.matDepthTargetID);
		matDepthPass.outputStates.push_back(D3D12_RESOURCE_STATE_DEPTH_WRITE);
		passes.push_back(matDepthPass);

		// classify pass.
		sl12::RenderPass classifyPass{};
		classifyPass.input.push_back(OutContainer.visibilityTargetID);
		classifyPass.input.push_back(OutContainer.gbufferTargetIDs[3]);
		classifyPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
		classifyPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
		passes.push_back(classifyPass);

		// material tile pass.
		sl12::RenderPass matTilePass{};
		matTilePass.input.push_back(OutContainer.visibilityTargetID);
		matTilePass.input.push_back(OutContainer.gbufferTargetIDs[3]);
		matTilePass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
		matTilePass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
		matTilePass.output.push_back(OutContainer.gbufferTargetIDs[0]);
		matTilePass.output.push_back(OutContainer.gbufferTargetIDs[1]);
		matTilePass.output.push_back(OutContainer.gbufferTargetIDs[2]);
		matTilePass.output.push_back(OutContainer.gbufferTargetIDs[3]);
		matTilePass.output.push_back(OutContainer.miplevelTargetID);
		matTilePass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
		matTilePass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
		matTilePass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
		matTilePass.outputStates.push_back(D3D12_RESOURCE_STATE_DEPTH_WRITE);
		matTilePass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		passes.push_back(matTilePass);
	}

	// feedback miplevel pass.
	sl12::RenderPass feedbackMipPass{};
	feedbackMipPass.input.push_back(OutContainer.miplevelTargetID);
	feedbackMipPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	passes.push_back(feedbackMipPass);

	// lighting pass.
	sl12::RenderPass lightingPass{};
	lightingPass.input.push_back(OutContainer.gbufferTargetIDs[0]);
	lightingPass.input.push_back(OutContainer.gbufferTargetIDs[1]);
	lightingPass.input.push_back(OutContainer.gbufferTargetIDs[2]);
	lightingPass.input.push_back(OutContainer.gbufferTargetIDs[3]);
	lightingPass.input.push_back(OutContainer.shadowExpTargetID);
	lightingPass.input.push_back(OutContainer.shadowDepthTargetID);
	lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	lightingPass.output.push_back(OutContainer.accumTargetID);
	lightingPass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	passes.push_back(lightingPass);

	if (bNeedDeinterleave)
	{
		// deinterleave pass.
		sl12::RenderPass diPass{};
		diPass.input.push_back(OutContainer.gbufferTargetIDs[3]);
		diPass.input.push_back(OutContainer.gbufferTargetIDs[2]);
		diPass.input.push_back(OutContainer.accumTargetID);
		diPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
		diPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
		diPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
		diPass.output.push_back(OutContainer.diDepthTargetID);
		diPass.output.push_back(OutContainer.diNormalTargetID);
		diPass.output.push_back(OutContainer.diAccumTargetID);
		diPass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		diPass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		diPass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		passes.push_back(diPass);
	}
	
	// ssao pass.
	sl12::RenderPass ssaoPass{};
	ssaoPass.input.push_back(OutContainer.gbufferTargetIDs[3]);
	ssaoPass.input.push_back(OutContainer.gbufferTargetIDs[2]);
	ssaoPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	ssaoPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	ssaoPass.output.push_back(OutContainer.ssaoTargetID);
	ssaoPass.output.push_back(OutContainer.ssgiTargetID);
	ssaoPass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	ssaoPass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	if (ssaoType_ == 2)
	{
		// ssgi.
		ssaoPass.input.push_back(OutContainer.accumTargetID);
		ssaoPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	}
	passes.push_back(ssaoPass);

	// ssao denoise pass.
	sl12::RenderPass denoisePass{};
	denoisePass.input.push_back(OutContainer.gbufferTargetIDs[3]);
	denoisePass.input.push_back(OutContainer.ssaoTargetID);
	denoisePass.input.push_back(OutContainer.ssgiTargetID);
	denoisePass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	denoisePass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	denoisePass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	if (depthHistory_ != sl12::kInvalidTargetID)
	{
		denoisePass.input.push_back(depthHistory_);
		denoisePass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	}
	if (ssaoHistory_ != sl12::kInvalidTargetID)
	{
		denoisePass.input.push_back(ssaoHistory_);
		denoisePass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	}
	if (ssgiHistory_ != sl12::kInvalidTargetID)
	{
		denoisePass.input.push_back(ssgiHistory_);
		denoisePass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	}
	denoisePass.output.push_back(OutContainer.denoiseTargetID);
	denoisePass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	denoisePass.output.push_back(OutContainer.denoiseGITargetID);
	denoisePass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	passes.push_back(denoisePass);
	histories.push_back(OutContainer.gbufferTargetIDs[3]);
	histories.push_back(OutContainer.denoiseTargetID);
	histories.push_back(OutContainer.denoiseGITargetID);

	// indirect lighting pass.
	sl12::RenderPass indirectPass{};
	indirectPass.input.push_back(OutContainer.gbufferTargetIDs[0]);
	indirectPass.input.push_back(OutContainer.gbufferTargetIDs[1]);
	indirectPass.input.push_back(OutContainer.gbufferTargetIDs[2]);
	indirectPass.input.push_back(OutContainer.gbufferTargetIDs[3]);
	indirectPass.input.push_back(OutContainer.denoiseTargetID);
	indirectPass.input.push_back(OutContainer.denoiseGITargetID);
	indirectPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	indirectPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	indirectPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	indirectPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	indirectPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	indirectPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	indirectPass.output.push_back(OutContainer.accumTargetID);
	indirectPass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	passes.push_back(indirectPass);

	// tonemap pass.
	sl12::RenderPass tonemapPass{};
	tonemapPass.input.push_back(OutContainer.accumTargetID);
	tonemapPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
	passes.push_back(tonemapPass);

	renderGraph_->CreateRenderPasses(&device_, passes, histories);
}

struct TemporalCB
{
	sl12::CbvHandle hSceneCB, hFrustumCB;
	sl12::CbvHandle hLightCB, hShadowCB;
	sl12::CbvHandle hDetailCB;
	sl12::CbvHandle hBlurXCB, hBlurYCB;
	sl12::CbvHandle hAmbOccCB;
	sl12::CbvHandle hDebugCB;
};
void SampleApplication::SetupConstantBuffers(struct TemporalCB& OutCBs)
{
	{
		DirectX::XMFLOAT3 upVec(0.0f, 1.0f, 0.0f);
		float Zn = 0.1f;
		auto cp = DirectX::XMLoadFloat3(&cameraPos_);
		auto dir = DirectX::XMLoadFloat3(&cameraDir_);
		auto up = DirectX::XMLoadFloat3(&upVec);
		auto mtxWorldToView = DirectX::XMMatrixLookAtRH(cp, DirectX::XMVectorAdd(cp, dir), up);
		auto mtxViewToClip = sl12::MatrixPerspectiveInfiniteInverseFovRH(DirectX::XMConvertToRadians(kFovY), (float)displayWidth_ / (float)displayHeight_, Zn);
		auto mtxWorldToClip = mtxWorldToView * mtxViewToClip;
		auto mtxClipToWorld = DirectX::XMMatrixInverse(nullptr, mtxWorldToClip);
		auto mtxViewToWorld = DirectX::XMMatrixInverse(nullptr, mtxWorldToView);
		auto mtxClipToView = DirectX::XMMatrixInverse(nullptr, mtxViewToClip);

		SceneCB cbScene;
		DirectX::XMStoreFloat4x4(&cbScene.mtxWorldToProj, mtxWorldToClip);
		DirectX::XMStoreFloat4x4(&cbScene.mtxWorldToView, mtxWorldToView);
		DirectX::XMStoreFloat4x4(&cbScene.mtxViewToProj, mtxViewToClip);
		DirectX::XMStoreFloat4x4(&cbScene.mtxProjToWorld, mtxClipToWorld);
		DirectX::XMStoreFloat4x4(&cbScene.mtxViewToWorld, mtxViewToWorld);
		DirectX::XMStoreFloat4x4(&cbScene.mtxProjToView, mtxClipToView);
		if (frameIndex_ == 0)
		{
			// first frame.
			auto U = DirectX::XMMatrixIdentity();
			DirectX::XMStoreFloat4x4(&cbScene.mtxProjToPrevProj, U);
			DirectX::XMStoreFloat4x4(&cbScene.mtxPrevViewToProj, mtxViewToClip);
		}
		else
		{
			auto mtxClipToPrevClip = mtxClipToWorld * mtxPrevWorldToClip_;
			DirectX::XMStoreFloat4x4(&cbScene.mtxProjToPrevProj, mtxClipToPrevClip);
			DirectX::XMStoreFloat4x4(&cbScene.mtxPrevViewToProj, mtxPrevViewToClip_);
		}
		cbScene.eyePosition.x = cameraPos_.x;
		cbScene.eyePosition.y = cameraPos_.y;
		cbScene.eyePosition.z = cameraPos_.z;
		cbScene.eyePosition.w = 0.0f;
		cbScene.screenSize.x = (float)displayWidth_;
		cbScene.screenSize.y = (float)displayHeight_;
		cbScene.invScreenSize.x = 1.0f / (float)displayWidth_;
		cbScene.invScreenSize.y = 1.0f / (float)displayHeight_;
		cbScene.nearFar.x = Zn;
		cbScene.nearFar.y = 0.0f;
		cbScene.feedbackIndex.x = (frameIndex_ % 16) % 4;
		cbScene.feedbackIndex.y = (frameIndex_ % 16) / 4;

		OutCBs.hSceneCB = cbvMan_->GetTemporal(&cbScene, sizeof(cbScene));

		FrustumCB cbFrustum;
		sl12::CalcFrustumPlanes(mtxWorldToClip, true, true, cbFrustum.frustumPlanes);
		OutCBs.hFrustumCB = cbvMan_->GetTemporal(&cbFrustum, sizeof(cbFrustum));

		mtxPrevWorldToView_ = mtxWorldToView;
		mtxPrevWorldToClip_ = mtxWorldToClip;
		mtxPrevViewToClip_ = mtxViewToClip;
	}
	{
		LightCB cbLight;
		
		memcpy(&cbLight.ambientSky, skyColor_, sizeof(cbLight.ambientSky));
		memcpy(&cbLight.ambientGround, groundColor_, sizeof(cbLight.ambientGround));
		cbLight.ambientIntensity = ambientIntensity_;

		auto dir = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		auto mtxRot = DirectX::XMMatrixRotationZ(DirectX::XMConvertToRadians(directionalTheta_)) * DirectX::XMMatrixRotationY(DirectX::XMConvertToRadians(directionalPhi_));
		dir = DirectX::XMVector3TransformNormal(dir, mtxRot);
		DirectX::XMFLOAT3 dirF3;
		DirectX::XMStoreFloat3(&dirF3, dir);
		memcpy(&cbLight.directionalVec, &dirF3, sizeof(cbLight.directionalVec));
		cbLight.directionalColor.x = directionalColor_[0] * directionalIntensity_;
		cbLight.directionalColor.y = directionalColor_[1] * directionalIntensity_;
		cbLight.directionalColor.z = directionalColor_[2] * directionalIntensity_;

		OutCBs.hLightCB = cbvMan_->GetTemporal(&cbLight, sizeof(cbLight));

		// NOTE: dirF3 is invert light vector.
		DirectX::XMFLOAT3 lightDir = DirectX::XMFLOAT3(-dirF3.x, -dirF3.y, -dirF3.z);

		auto front = DirectX::XMVectorSet(-dirF3.x, -dirF3.y, -dirF3.z, 0.0f);
		auto up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		auto right = DirectX::XMVector3Cross(front, up);
		auto lenV = DirectX::XMVector3Length(right);
		float len;
		DirectX::XMStoreFloat(&len, lenV);
		if (len < 1e-4f)
		{
			up = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
			right = DirectX::XMVector3Cross(front, up);
		}
		right = DirectX::XMVector3Normalize(right);
		up = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(right, front));

		DirectX::XMVECTOR pnts[] = {
			DirectX::XMVectorSet(sceneAABBMax_.x, sceneAABBMax_.y, sceneAABBMax_.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMax_.x, sceneAABBMax_.y, sceneAABBMin_.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMax_.x, sceneAABBMin_.y, sceneAABBMax_.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMax_.x, sceneAABBMin_.y, sceneAABBMin_.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMin_.x, sceneAABBMax_.y, sceneAABBMax_.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMin_.x, sceneAABBMax_.y, sceneAABBMin_.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMin_.x, sceneAABBMin_.y, sceneAABBMax_.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMin_.x, sceneAABBMin_.y, sceneAABBMin_.z, 1.0f),
		};
		DirectX::XMFLOAT3 lsAABBMax(-FLT_MAX, -FLT_MAX, -FLT_MAX), lsAABBMin(FLT_MAX, FLT_MAX, FLT_MAX);
		for (auto pnt : pnts)
		{
			float t;
			auto v = DirectX::XMVector3Dot(right, pnt);
			DirectX::XMStoreFloat(&t, v);
			lsAABBMax.x = std::max(lsAABBMax.x, t);
			lsAABBMin.x = std::min(lsAABBMin.x, t);

			v = DirectX::XMVector3Dot(up, pnt);
			DirectX::XMStoreFloat(&t, v);
			lsAABBMax.y = std::max(lsAABBMax.y, t);
			lsAABBMin.y = std::min(lsAABBMin.y, t);

			v = DirectX::XMVector3Dot(front, pnt);
			DirectX::XMStoreFloat(&t, v);
			lsAABBMax.z = std::max(lsAABBMax.z, t);
			lsAABBMin.z = std::min(lsAABBMin.z, t);
		}
		float width = std::max(lsAABBMax.x - lsAABBMin.x, lsAABBMax.y - lsAABBMin.y);
		auto cp = DirectX::XMVectorScale(right, (lsAABBMax.x + lsAABBMin.x) * 0.5f);
		cp = DirectX::XMVectorAdd(DirectX::XMVectorScale(up, (lsAABBMax.y + lsAABBMin.y) * 0.5f), cp);
		cp = DirectX::XMVectorAdd(DirectX::XMVectorScale(front, lsAABBMin.z), cp);
		auto mtxWorldToView = DirectX::XMMatrixLookAtRH(cp, DirectX::XMVectorAdd(cp, front), up);
		auto mtxViewToClip = sl12::MatrixOrthoInverseFovRH(width, width, 0.0f, lsAABBMax.z - lsAABBMin.z);
		auto mtxWorldToClip = mtxWorldToView * mtxViewToClip;

		ShadowCB cbShadow;
		DirectX::XMStoreFloat4x4(&cbShadow.mtxWorldToProj, mtxWorldToClip);
		cbShadow.exponent = DirectX::XMFLOAT2(shadowExponent_, shadowExponent_);
		cbShadow.constBias = shadowBias_;

		OutCBs.hShadowCB = cbvMan_->GetTemporal(&cbShadow, sizeof(cbShadow));
	}
	{
		const float kSigma = 2.0f;
		float gaussianKernels[5];
		float total = 0.0f;
		for (int i = 0; i < 5; i++)
		{
			gaussianKernels[i] = std::expf(-0.5f * i * i / kSigma);
			total += (i == 0) ? gaussianKernels[i] : gaussianKernels[i] * 2.0f;
		}
		for (int i = 0; i < 5; i++)
		{
			gaussianKernels[i] /= total;
		}

		BlurCB cbBlur;
		cbBlur.kernel0 = DirectX::XMFLOAT4(gaussianKernels[0], gaussianKernels[1], gaussianKernels[2], gaussianKernels[3]);
		cbBlur.kernel1 = DirectX::XMFLOAT4(gaussianKernels[4], 0.0f, 0.0f, 0.0f);
		cbBlur.offset = DirectX::XMFLOAT2(1.5f / (float)displayWidth_, 0.0f);
		OutCBs.hBlurXCB = cbvMan_->GetTemporal(&cbBlur, sizeof(cbBlur));

		cbBlur.offset = DirectX::XMFLOAT2(0.0f, 1.5f / (float)displayHeight_);
		OutCBs.hBlurYCB = cbvMan_->GetTemporal(&cbBlur, sizeof(cbBlur));
	}
	{
		DetailCB cbDetail;

		cbDetail.detailType = detailType_;
		cbDetail.detailTile = DirectX::XMFLOAT2(detailTile_, detailTile_);
		cbDetail.detailIntensity = detailIntensity_;
		cbDetail.triplanarType = triplanarType_;
		cbDetail.triplanarTile = triplanarTile_;

		OutCBs.hDetailCB = cbvMan_->GetTemporal(&cbDetail, sizeof(cbDetail));
	}
	{
		AmbOccCB cbAO;

		cbAO.intensity = ssaoIntensity_;
		cbAO.giIntensity = ssgiIntensity_;
		cbAO.thickness = ssaoConstThickness_;
		cbAO.sliceCount = ssaoSliceCount_;
		cbAO.stepCount = ssaoStepCount_;
		cbAO.tangentBias = ssaoTangentBias_;
		cbAO.temporalIndex = (sl12::u32)(frameIndex_ & 0xff);
		cbAO.maxPixelRadius = ssaoMaxPixel_;
		cbAO.worldSpaceRadius = ssaoWorldRadius_;
		cbAO.baseVecType = ssaoBaseVecType_;
		cbAO.viewBias = ssaoViewBias_;

		float focalLen = ((float)displayHeight_ / (float)displayWidth_) * (tanf(DirectX::XMConvertToRadians(kFovY) * 0.5f));
		cbAO.ndcPixelSize = 0.5f * (float)displayWidth_ / focalLen;

		cbAO.denoiseRadius = denoiseRadius_;
		cbAO.denoiseBaseWeight = denoiseBaseWeight_;
		cbAO.denoiseDepthSigma = denoiseDepthSigma_;

		OutCBs.hAmbOccCB = cbvMan_->GetTemporal(&cbAO, sizeof(cbAO));
	}
	{
		DebugCB cbDebug;

		cbDebug.displayMode = displayMode_;

		OutCBs.hDebugCB = cbvMan_->GetTemporal(&cbDebug, sizeof(cbDebug));
	}
}

bool SampleApplication::Execute()
{
	const int kSwapchainBufferOffset = 1;
	auto frameIndex = (device_.GetSwapchain().GetFrameIndex() + sl12::Swapchain::kMaxBuffer - 1) % sl12::Swapchain::kMaxBuffer;
	auto prevFrameIndex = (device_.GetSwapchain().GetFrameIndex() + sl12::Swapchain::kMaxBuffer - 2) % sl12::Swapchain::kMaxBuffer;
	auto pCmdList = &mainCmdList_->Reset();
	auto* pTimestamp = timestamps_ + timestampIndex_;

	sl12::CpuTimer now = sl12::CpuTimer::CurrentTime();
	sl12::CpuTimer delta = now - currCpuTime_;
	currCpuTime_ = now;

	ControlCamera(delta.ToSecond());
	gui_->BeginNewFrame(pCmdList, displayWidth_, displayHeight_, inputData_);
	inputData_.Reset();
	{
		bool bPrevMode = bEnableVisibilityBuffer_;

		// rendering settings.
		if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Checkbox("Visibility Buffer", &bEnableVisibilityBuffer_))
			{}
		}

		// light settings.
		if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::ColorEdit3("Ambient Sky Color", skyColor_);
			ImGui::ColorEdit3("Ambient Ground Color", groundColor_);
			ImGui::SliderFloat("Ambient Intensity", &ambientIntensity_, 0.0f, 10.0f);
			ImGui::SliderFloat("Directional Theta", &directionalTheta_, 0.0f, 90.0f);
			ImGui::SliderFloat("Directional Phi", &directionalPhi_, 0.0f, 360.0f);
			ImGui::ColorEdit3("Directional Color", directionalColor_);
			ImGui::SliderFloat("Directional Intensity", &directionalIntensity_, 0.0f, 10.0f);
		}

		// shadow settings.
		if (ImGui::CollapsingHeader("Shadow", ImGuiTreeNodeFlags_DefaultOpen))
		{
#if SHADOW_TYPE == 1
			ImGui::Checkbox("Blur", &evsmBlur_);
			ImGui::SliderFloat("Exponent", &shadowExponent_, 0.1f, 50.0f);
#endif
			ImGui::SliderFloat("Constant Bias", &shadowBias_, 0.001f, 0.02f);
		}

		// detail normal settings.
		if (ImGui::CollapsingHeader("Surface Gradient", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static const char* kDetailTypes[] = {
				"None",
				"UDN",
				"Surface Gradient",
				"Surface Gradient Tex",
			};
			static const char* kTriplanarTypes[] = {
				"Blend",
				"Surface Gradient",
			};
			ImGui::Combo("Detail Type", &detailType_, kDetailTypes, ARRAYSIZE(kDetailTypes));
			ImGui::SliderFloat("Detail Tiling", &detailTile_, 1.0f, 10.0f);
			ImGui::SliderFloat("Detail Intensity", &detailIntensity_, 0.0f, 2.0f);
			ImGui::Combo("Triplanar Type", &triplanarType_, kTriplanarTypes, ARRAYSIZE(kTriplanarTypes));
			ImGui::SliderFloat("Triplanar Tiling", &triplanarTile_, 0.001f, 0.1f);
		}

		// ssao settings.
		if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static const char* kTypes[] = {
				"HBAO",
				"Visibility Bitmask",
				"SSGI with VB",
			};
			ImGui::Combo("Type", &ssaoType_, kTypes, ARRAYSIZE(kTypes));
			ImGui::SliderFloat("Intensity", &ssaoIntensity_, 0.0f, 10.0f);
			ImGui::SliderFloat("GI Intensity", &ssgiIntensity_, 0.0f, 40.0f);
			ImGui::SliderInt("Slice Count", &ssaoSliceCount_, 1, 16);
			ImGui::SliderInt("Step Count", &ssaoStepCount_, 1, 16);
			ImGui::SliderInt("Max Pixel", &ssaoMaxPixel_, 1, 512);
			ImGui::SliderFloat("World Radius", &ssaoWorldRadius_, 0.0f, 200.0f);
			ImGui::SliderFloat("Tangent Bias", &ssaoTangentBias_, 0.0f, 1.0f);
			ImGui::SliderFloat("Thickness", &ssaoConstThickness_, 0.1f, 10.0f);
			ImGui::SliderFloat("View Bias", &ssaoViewBias_, 0.0f, 10.0f);
			static const char* kBaseVecs[] = {
				"Pixel Normal",
				"View Vector",
				"Face Normal",
			};
			ImGui::Combo("Baes Vec", &ssaoBaseVecType_, kBaseVecs, ARRAYSIZE(kBaseVecs));
			ImGui::Checkbox("Deinterleave", &bIsDeinterleave_);
		}

		// denoise settings.
		if (ImGui::CollapsingHeader("Denoise", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat("Spatio Radius", &denoiseRadius_, 0.0f, 5.0f);
			ImGui::SliderFloat("Base Weight", &denoiseBaseWeight_, 0.0f, 0.99f);
			ImGui::SliderFloat("Depth Sigma", &denoiseDepthSigma_, 0.0f, 20.0f);
		}

		// debug settings.
		if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static const char* kDisplayModes[] = {
				"Lighting",
				"BaseColor",
				"Roughness",
				"Metallic",
				"World Normal",
				"AO",
				"GI",
			};
			ImGui::Combo("Display Mode", &displayMode_, kDisplayModes, ARRAYSIZE(kDisplayModes));

			ImGui::Checkbox("Texture Streaming", &bIsTexStreaming_);
			if (ImGui::Button("Miplevel Print"))
			{
				for (auto&& work : workMaterials_)
				{
					if (!work.pResMaterial->baseColorTex.IsValid())
						continue;
					
					auto TexBase = work.pResMaterial->baseColorTex.GetItem<sl12::ResourceItemTextureBase>();
					if (TexBase->IsSameSubType(sl12::ResourceItemStreamingTexture::kSubType))
					{
						auto Tex = work.pResMaterial->baseColorTex.GetItem<sl12::ResourceItemStreamingTexture>();
						sl12::ConsolePrint("TexMiplevel: %s (%d)\n", Tex->GetFilePath().c_str(), Tex->GetCurrMipLevel());
					}
				}
			}
		}

		// gpu performance.
		if (ImGui::CollapsingHeader("GPU Time", ImGuiTreeNodeFlags_DefaultOpen))
		{
			uint64_t freq = device_.GetGraphicsQueue().GetTimestampFrequency();
			uint64_t timestamp[16];
			pTimestamp->GetTimestamp(0, 16, timestamp);

			int timeIndex = 1;
			auto GetTime = [&]()
			{
				int index = timeIndex++;
				return timestamp[index + 1] - timestamp[index];
			};
			auto GetTotalTime = [&]()
			{
				return timestamp[timeIndex + 1] - timestamp[0];
			};
			auto GetMS = [freq](uint64_t t)
			{
				return (float)t / ((float)freq / 1000.0f);
			};
			if (!bPrevMode)
			{
				uint64_t gbuffer = GetTime();
				uint64_t lighting = GetTime();
				uint64_t ssao = GetTime();
				uint64_t indirect = GetTime();
				uint64_t tonemap = GetTime();
				uint64_t total = GetTotalTime();

				ImGui::Text("Total   : %f (ms)", GetMS(total));
				ImGui::Text("GBuffer : %f (ms)", GetMS(gbuffer));
				ImGui::Text("Lighting: %f (ms)", GetMS(lighting));
				ImGui::Text("SSAO    : %f (ms)", GetMS(ssao));
				ImGui::Text("Indirect: %f (ms)", GetMS(indirect));
				ImGui::Text("Tonemap : %f (ms)", GetMS(tonemap));
			}
			else
			{
				uint64_t visibility = GetTime();
				uint64_t depth = GetTime();
				uint64_t classify = GetTime();
				uint64_t tile = GetTime();
				uint64_t lighting = GetTime();
				uint64_t ssao = GetTime();
				uint64_t indirect = GetTime();
				uint64_t tonemap = GetTime();
				uint64_t total = GetTotalTime();

				ImGui::Text("Total      : %f (ms)", GetMS(total));
				ImGui::Text("Visibility : %f (ms)", GetMS(visibility));
				ImGui::Text("Depth      : %f (ms)", GetMS(depth));
				ImGui::Text("Classify   : %f (ms)", GetMS(classify));
				ImGui::Text("MatTile    : %f (ms)", GetMS(tile));
				ImGui::Text("Lighting   : %f (ms)", GetMS(lighting));
				ImGui::Text("SSAO       : %f (ms)", GetMS(ssao));
				ImGui::Text("Indirect   : %f (ms)", GetMS(indirect));
				ImGui::Text("Tonemap    : %f (ms)", GetMS(tonemap));
			}
		}
	}
	ImGui::Render();

	bool bNeedDeinterleave = bIsDeinterleave_ && (ssaoType_ == 2);
	
	device_.WaitPresent();
	device_.SyncKillObjects();

	pTimestamp->Reset();
	pTimestamp->Query(pCmdList);
	device_.LoadRenderCommands(pCmdList);
	meshMan_->BeginNewFrame(pCmdList);
	cbvMan_->BeginNewFrame();
	renderGraph_->BeginNewFrame();

	if (!detailDerivTex_.IsValid())
	{
		detailDerivTex_ = sl12::MakeUnique<sl12::Texture>(&device_);
		detailDerivSrv_ = sl12::MakeUnique<sl12::TextureView>(&device_);

		auto detail_res = hDetailTex_.GetItem<sl12::ResourceItemTextureBase>();
		sl12::TextureDesc desc = detail_res->GetTexture().GetTextureDesc();
		desc.format = DXGI_FORMAT_R8G8_UNORM;
		desc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess;
		desc.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		
		detailDerivTex_->Initialize(&device_, desc);
		detailDerivSrv_->Initialize(&device_, &detailDerivTex_);

		UniqueHandle<sl12::UnorderedAccessView> uav = sl12::MakeUnique<sl12::UnorderedAccessView>(&device_);
		uav->Initialize(&device_, &detailDerivTex_);

		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetCsSrv(0, detail_res->GetTextureView().GetDescInfo().cpuHandle);
		descSet.SetCsUav(0, uav->GetDescInfo().cpuHandle);

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoNormalToDeriv_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rsCs_, &descSet);

		// dispatch.
		UINT x = (desc.width + 7) / 8;
		UINT y = (desc.height + 7) / 8;
		pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
		
		pCmdList->TransitionBarrier(&detailDerivTex_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
	}

	// setup render graph.
	TargetIDContainer TargetContainer;
	SetupRenderGraph(TargetContainer);

	// create scene constant buffer.
	TemporalCB TempCB;
	SetupConstantBuffers(TempCB);

	// clear swapchain.
	auto&& swapchain = device_.GetSwapchain();
	pCmdList->TransitionBarrier(swapchain.GetCurrentTexture(kSwapchainBufferOffset), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	{
		float color[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
		pCmdList->GetLatestCommandList()->ClearRenderTargetView(swapchain.GetCurrentRenderTargetView(kSwapchainBufferOffset)->GetDescInfo().cpuHandle, color, 0, nullptr);
	}

	// create mesh cbuffers.
	std::vector<sl12::CbvHandle> MeshCBs;
	for (auto&& mesh : sceneMeshes_)
	{
		// set mesh constant.
		MeshCB cbMesh;
		cbMesh.mtxLocalToWorld = mesh->GetMtxLocalToWorld();
		cbMesh.mtxPrevLocalToWorld = mesh->GetMtxPrevLocalToWorld();
		MeshCBs.push_back(cbvMan_->GetTemporal(&cbMesh, sizeof(cbMesh)));
	}

	// create meshlet buffers.
	D3D12_RESOURCE_STATES indirectArgBufferState = D3D12_RESOURCE_STATE_INDEX_BUFFER;
	if (!indirectArgBuffer_.IsValid() || !indirectArgUpload_.IsValid())
	{
		indirectArgBuffer_.Reset();
		indirectArgUpload_.Reset();
		indirectArgUAV_.Reset();
		meshletCBs_.clear();
		indirectArgBufferState = D3D12_RESOURCE_STATE_COMMON;

		sl12::u32 numMeshlets = 0;
		for (auto&& mesh : sceneMeshes_)
		{
			auto&& submeshes = mesh->GetParentResource()->GetSubmeshes();
			sl12::u32 cnt = 0;
			for (auto&& submesh : submeshes)
			{
				cnt += (sl12::u32)submesh.meshlets.size();
			}

			MeshletCullCB cb;
			cb.meshletStartIndex = numMeshlets;
			cb.meshletCount = cnt;
			cb.argStartAddress = numMeshlets * kIndirectArgsBufferStride;

			auto handle = cbvMan_->GetResident(sizeof(cb));
			cbvMan_->RequestResidentCopy(handle, &cb, sizeof(cb));
			meshletCBs_.push_back(std::move(handle));

			numMeshlets += cnt;
		}
		cbvMan_->ExecuteCopy(pCmdList);

		// create buffers.
		indirectArgBuffer_ = sl12::MakeUnique<sl12::Buffer>(&device_);
		indirectArgUpload_ = sl12::MakeUnique<sl12::Buffer>(&device_);
		indirectArgUAV_ = sl12::MakeUnique<sl12::UnorderedAccessView>(&device_);

		sl12::BufferDesc desc{};
		desc.stride = kIndirectArgsBufferStride;
		desc.size = desc.stride * numMeshlets + 4/* overflow support. */;
		desc.usage = sl12::ResourceUsage::UnorderedAccess;
		desc.heap = sl12::BufferHeap::Default;
		desc.initialState = D3D12_RESOURCE_STATE_COMMON;
		indirectArgBuffer_->Initialize(&device_, desc);

		desc.usage = sl12::ResourceUsage::Unknown;
		desc.heap = sl12::BufferHeap::Dynamic;
		indirectArgUpload_->Initialize(&device_, desc);

		indirectArgUAV_->Initialize(&device_, &indirectArgBuffer_, 0, 0, 0, 0);

		// build upload buffer.
		sl12::u8* pUpload = static_cast<sl12::u8*>(indirectArgUpload_->Map());
		for (auto&& mesh : sceneMeshes_)
		{
			auto&& submeshes = mesh->GetParentResource()->GetSubmeshes();
			for (auto&& submesh : submeshes)
			{
				UINT StartIndexLocation = (UINT)(submesh.indexOffsetBytes / sl12::ResourceItemMesh::GetIndexStride());
				int BaseVertexLocation = (int)(submesh.positionOffsetBytes / sl12::ResourceItemMesh::GetPositionStride());
				
				for (auto&& meshlet : submesh.meshlets)
				{
					// root const.
					memset(pUpload, 0, sizeof(sl12::u32));
					pUpload += sizeof(sl12::u32);

					// draw arg.
					D3D12_DRAW_INDEXED_ARGUMENTS arg{};
					arg.IndexCountPerInstance = meshlet.indexCount;
					arg.InstanceCount = 1;
					arg.StartIndexLocation = StartIndexLocation + meshlet.indexOffset;
					arg.BaseVertexLocation = BaseVertexLocation;
					arg.StartInstanceLocation = 0;
					memcpy(pUpload, &arg, sizeof(arg));
					pUpload += sizeof(arg);
				}
			}
		}
		indirectArgUpload_->Unmap();
	}
	// copy default indirect arg buffer.
	pCmdList->TransitionBarrier(&indirectArgBuffer_, indirectArgBufferState, D3D12_RESOURCE_STATE_COPY_DEST);
	pCmdList->GetLatestCommandList()->CopyResource(indirectArgBuffer_->GetResourceDep(), indirectArgUpload_->GetResourceDep());

	// shadow depth pass.
	renderGraph_->NextPass(pCmdList);
	{
		GPU_MARKER(pCmdList, 0, "ShadowDepthPass");

		// output barrier.
		renderGraph_->BarrierOutputsAll(pCmdList);

		// clear rt.
		D3D12_CPU_DESCRIPTOR_HANDLE dsv = renderGraph_->GetTarget(TargetContainer.shadowDepthTargetID)->dsvs[0]->GetDescInfo().cpuHandle;
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		pCmdList->GetLatestCommandList()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

		// set render targets.
		pCmdList->GetLatestCommandList()->OMSetRenderTargets(0, nullptr, false, &dsv);

		// set viewport.
		D3D12_VIEWPORT vp;
		vp.TopLeftX = vp.TopLeftY = 0.0f;
		vp.Width = (float)kShadowMapSize;
		vp.Height = (float)kShadowMapSize;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

		// set scissor rect.
		D3D12_RECT rect;
		rect.left = rect.top = 0;
		rect.right = kShadowMapSize;
		rect.bottom = kShadowMapSize;
		pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetVsCbv(0, TempCB.hShadowCB.GetCBV()->GetDescInfo().cpuHandle);

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoShadowDepth_->GetPSO());
		pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// draw meshes.
		uint meshIndex = 0;
		for (auto&& mesh : sceneMeshes_)
		{
			// set mesh constant.
			descSet.SetVsCbv(1, MeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);

			auto meshRes = mesh->GetParentResource();

			// set vertex buffer.
			const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
				sl12::MeshManager::CreateVertexView(meshRes->GetPositionHandle(), 0, 0, sl12::ResourceItemMesh::GetPositionStride()),
			};
			pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

			// set index buffer.
			auto ibv = sl12::MeshManager::CreateIndexView(meshRes->GetIndexHandle(), 0, 0, sl12::ResourceItemMesh::GetIndexStride());
			pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

			auto&& submeshes = meshRes->GetSubmeshes();
			auto submesh_count = submeshes.size();
			for (int i = 0; i < submesh_count; i++)
			{
				auto&& submesh = submeshes[i];

				pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);

				UINT StartIndexLocation = (UINT)(submesh.indexOffsetBytes / sl12::ResourceItemMesh::GetIndexStride());
				int BaseVertexLocation = (int)(submesh.positionOffsetBytes / sl12::ResourceItemMesh::GetPositionStride());

				pCmdList->GetLatestCommandList()->DrawIndexedInstanced(submesh.indexCount, 1, StartIndexLocation, BaseVertexLocation, 0);
			}

			meshIndex++;
		}
	}
	renderGraph_->EndPass();

#if SHADOW_TYPE == 1
	// shadow exponent pass.
	renderGraph_->NextPass(pCmdList);
	{
		GPU_MARKER(pCmdList, 0, "ShadowExp");

		// output barrier.
		renderGraph_->BarrierOutputsAll(pCmdList);

		// set render targets.
		D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderGraph_->GetTarget(TargetContainer.shadowExpTargetID)->rtvs[0]->GetDescInfo().cpuHandle;
		pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, nullptr);

		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetPsCbv(0, TempCB.hShadowCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetPsSrv(0, renderGraph_->GetTarget(TargetContainer.shadowDepthTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoShadowExp_->GetPSO());
		pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);

		// draw fullscreen.
		pCmdList->GetLatestCommandList()->DrawIndexedInstanced(3, 1, 0, 0, 0);
	}
	renderGraph_->EndPass();

	if (evsmBlur_)
	{
		// gaussian blur pass.
		renderGraph_->NextPass(pCmdList);
		{
			GPU_MARKER(pCmdList, 0, "GaussianBlurX");

			// output barrier.
			renderGraph_->BarrierOutputsAll(pCmdList);

			// set render targets.
			D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderGraph_->GetTarget(TargetContainer.shadowExpTmpTargetID)->rtvs[0]->GetDescInfo().cpuHandle;
			pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, nullptr);

			// set descriptors.
			sl12::DescriptorSet descSet;
			descSet.Reset();
			descSet.SetPsCbv(0, TempCB.hBlurXCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(0, renderGraph_->GetTarget(TargetContainer.shadowExpTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetPsSampler(0, linearClampSampler_->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoBlur_->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);

			// draw fullscreen.
			pCmdList->GetLatestCommandList()->DrawIndexedInstanced(3, 1, 0, 0, 0);
		}
		renderGraph_->EndPass();
		renderGraph_->NextPass(pCmdList);
		{
			GPU_MARKER(pCmdList, 0, "GaussianBlurY");

			// output barrier.
			renderGraph_->BarrierOutputsAll(pCmdList);

			// set render targets.
			D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderGraph_->GetTarget(TargetContainer.shadowExpTargetID)->rtvs[0]->GetDescInfo().cpuHandle;
			pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, nullptr);

			// set descriptors.
			sl12::DescriptorSet descSet;
			descSet.Reset();
			descSet.SetPsCbv(0, TempCB.hBlurYCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(0, renderGraph_->GetTarget(TargetContainer.shadowExpTmpTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetPsSampler(0, linearClampSampler_->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoBlur_->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);

			// draw fullscreen.
			pCmdList->GetLatestCommandList()->DrawIndexedInstanced(3, 1, 0, 0, 0);
		}
		renderGraph_->EndPass();
	}
#endif

	// meshlet culling.
	{
		// transition meshlet arg buffer.
		pCmdList->TransitionBarrier(&indirectArgBuffer_, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		
		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetCsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetCsCbv(1, TempCB.hFrustumCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetCsUav(0, indirectArgUAV_->GetDescInfo().cpuHandle);

		sl12::u32 meshIndex = 0;
		for (auto&& mesh : sceneMeshes_)
		{
			// set mesh constant.
			descSet.SetCsCbv(2, MeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetCsCbv(3, meshletCBs_[meshIndex].GetCBV()->GetDescInfo().cpuHandle);

			sl12::u32 meshletCnt = 0;
			if (hSuzanneMesh_.IsValid() && mesh->GetParentResource() == hSuzanneMesh_.GetItem<sl12::ResourceItemMesh>())
			{
				descSet.SetCsSrv(0, SuzanneMeshletBV_->GetDescInfo().cpuHandle);
				meshletCnt = (sl12::u32)(SuzanneMeshletB_->GetBufferDesc().size / SuzanneMeshletB_->GetBufferDesc().stride);
			}
			else if (hSponzaMesh_.IsValid() && mesh->GetParentResource() == hSponzaMesh_.GetItem<sl12::ResourceItemMesh>())
			{
				descSet.SetCsSrv(0, SponzaMeshletBV_->GetDescInfo().cpuHandle);
				meshletCnt = (sl12::u32)(SponzaMeshletB_->GetBufferDesc().size / SponzaMeshletB_->GetBufferDesc().stride);
			}
			else if (hCurtainMesh_.IsValid() && mesh->GetParentResource() == hCurtainMesh_.GetItem<sl12::ResourceItemMesh>())
			{
				descSet.SetCsSrv(0, CurtainMeshletBV_->GetDescInfo().cpuHandle);
				meshletCnt = (sl12::u32)(CurtainMeshletB_->GetBufferDesc().size / CurtainMeshletB_->GetBufferDesc().stride);
			}
			else if (hSphereMesh_.IsValid() && mesh->GetParentResource() == hSphereMesh_.GetItem<sl12::ResourceItemMesh>())
			{
				descSet.SetCsSrv(0, SphereMeshletBV_->GetDescInfo().cpuHandle);
				meshletCnt = (sl12::u32)(SphereMeshletB_->GetBufferDesc().size / SphereMeshletB_->GetBufferDesc().stride);
			}

			pCmdList->GetLatestCommandList()->SetPipelineState(psoMeshletCull_->GetPSO());
			pCmdList->SetComputeRootSignatureAndDescriptorSet(&rsCs_, &descSet);

			UINT t = (meshletCnt + 32 - 1) / 32;
			pCmdList->GetLatestCommandList()->Dispatch(t, 1, 1);
			
			meshIndex++;
		}		

		// transition meshlet arg buffer.
		pCmdList->TransitionBarrier(&indirectArgBuffer_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	}

	// clear miplevel pass.
	renderGraph_->NextPass(pCmdList);
	{
		GPU_MARKER(pCmdList, 1, "ClearMiplevelPass");

		// output barrier.
		renderGraph_->BarrierOutputsAll(pCmdList);

		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetCsUav(0, renderGraph_->GetTarget(TargetContainer.miplevelTargetID)->uavs[0]->GetDescInfo().cpuHandle);

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoClearMip_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rsCs_, &descSet);

		// dispatch.
		UINT w = (displayWidth_ + 3) / 4;
		UINT h = (displayHeight_ + 3) / 4;
		UINT x = (w + 7) / 8;
		UINT y = (w + 7) / 8;
		pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);

		// feedback clear.
		pCmdList->TransitionBarrier(&miplevelBuffer_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
		pCmdList->GetLatestCommandList()->CopyResource(miplevelBuffer_->GetResourceDep(), miplevelCopySrc_->GetResourceDep());
	}
	renderGraph_->EndPass();
	
	if (!bEnableVisibilityBuffer_)
	{
		// if indirect executer is NOT existed, initialize it.
		if (!meshletIndirectStandard_.IsValid())
		{
			meshletIndirectStandard_ = sl12::MakeUnique<sl12::IndirectExecuter>(&device_);
			bool bIndirectExecuterSucceeded = meshletIndirectStandard_->Initialize(&device_, sl12::IndirectType::DrawIndexed, kIndirectArgsBufferStride);
			assert(bIndirectExecuterSucceeded);
		}

		// depth pre pass.
		renderGraph_->NextPass(pCmdList);
		{
			GPU_MARKER(pCmdList, 0, "DepthPrePass");

			// output barrier.
			renderGraph_->BarrierOutputsAll(pCmdList);

			// clear depth.
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[3])->dsvs[0]->GetDescInfo().cpuHandle;
			pCmdList->GetLatestCommandList()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

			// set render targets.
			pCmdList->GetLatestCommandList()->OMSetRenderTargets(0, nullptr, false, &dsv);

			// set viewport.
			D3D12_VIEWPORT vp;
			vp.TopLeftX = vp.TopLeftY = 0.0f;
			vp.Width = (float)displayWidth_;
			vp.Height = (float)displayHeight_;
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

			// set scissor rect.
			D3D12_RECT rect;
			rect.left = rect.top = 0;
			rect.right = displayWidth_;
			rect.bottom = displayHeight_;
			pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

			// set descriptors.
			auto detail_res = const_cast<sl12::ResourceItemTextureBase*>(hDetailTex_.GetItem<sl12::ResourceItemTextureBase>());
			sl12::DescriptorSet descSet;
			descSet.Reset();
			descSet.SetVsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);

			pCmdList->GetLatestCommandList()->SetPipelineState(psoDepth_->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// draw meshes.
			sl12::u32 meshIndex = 0;
			sl12::u32 meshletTotal = 0;
			for (auto&& mesh : sceneMeshes_)
			{
				auto meshRes = mesh->GetParentResource();
				
				// set mesh constant.
				descSet.SetVsCbv(1, MeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);
				pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);

				// set vertex buffer.
				const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
					sl12::MeshManager::CreateVertexView(meshRes->GetPositionHandle(), 0, 0, sl12::ResourceItemMesh::GetPositionStride()),
				};
				pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

				// set index buffer.
				auto ibv = sl12::MeshManager::CreateIndexView(meshRes->GetIndexHandle(), 0, 0, sl12::ResourceItemMesh::GetIndexStride());
				pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

				auto&& submeshes = meshRes->GetSubmeshes();
				auto submesh_count = submeshes.size();
				for (int i = 0; i < submesh_count; i++)
				{
					auto&& submesh = submeshes[i];
					sl12::u32 meshletCnt = (sl12::u32)submesh.meshlets.size();

					pCmdList->GetLatestCommandList()->ExecuteIndirect(
						meshletIndirectStandard_->GetCommandSignature(),			// command signature
						meshletCnt,													// max command count
						indirectArgBuffer_->GetResourceDep(),						// argument buffer
						meshletIndirectStandard_->GetStride() * meshletTotal + 4,	// argument buffer offset
						nullptr,													// count buffer
						0);											// count buffer offset

					meshletTotal += meshletCnt;
				}

				meshIndex++;
			}
		}
		renderGraph_->EndPass();
		
		// gbuffer pass.
		pTimestamp->Query(pCmdList);
		renderGraph_->NextPass(pCmdList);
		{
			GPU_MARKER(pCmdList, 0, "GBufferPass");

			// output barrier.
			renderGraph_->BarrierOutputsAll(pCmdList);

			// clear depth.
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[3])->dsvs[0]->GetDescInfo().cpuHandle;
			pCmdList->GetLatestCommandList()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

			// set render targets.
			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
				renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[0])->rtvs[0]->GetDescInfo().cpuHandle,
				renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[1])->rtvs[0]->GetDescInfo().cpuHandle,
				renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[2])->rtvs[0]->GetDescInfo().cpuHandle,
			};
			pCmdList->GetLatestCommandList()->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, false, &dsv);

			// set viewport.
			D3D12_VIEWPORT vp;
			vp.TopLeftX = vp.TopLeftY = 0.0f;
			vp.Width = (float)displayWidth_;
			vp.Height = (float)displayHeight_;
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

			// set scissor rect.
			D3D12_RECT rect;
			rect.left = rect.top = 0;
			rect.right = displayWidth_;
			rect.bottom = displayHeight_;
			pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

			// set descriptors.
			auto detail_res = const_cast<sl12::ResourceItemTextureBase*>(hDetailTex_.GetItem<sl12::ResourceItemTextureBase>());
			sl12::DescriptorSet descSet;
			descSet.Reset();
			descSet.SetVsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetPsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetPsCbv(1, TempCB.hDetailCB.GetCBV()->GetDescInfo().cpuHandle);
			if (detailType_ != 3)
			{
				descSet.SetPsSrv(3, detail_res->GetTextureView().GetDescInfo().cpuHandle);
			}
			else
			{
				descSet.SetPsSrv(3, detailDerivSrv_->GetDescInfo().cpuHandle);
			}
			descSet.SetPsSampler(0, linearSampler_->GetDescInfo().cpuHandle);
			descSet.SetPsUav(0, renderGraph_->GetTarget(TargetContainer.miplevelTargetID)->uavs[0]->GetDescInfo().cpuHandle);

			sl12::GraphicsPipelineState* NowPSO = nullptr;

			// draw meshes.
			sl12::u32 meshIndex = 0;
			sl12::u32 meshletTotal = 0;
			for (auto&& mesh : sceneMeshes_)
			{
				// select pso.
				auto meshRes = mesh->GetParentResource();
				sl12::GraphicsPipelineState* pso = &psoMesh_;
				if (hSphereMesh_.IsValid() && meshRes == hSphereMesh_.GetItem<sl12::ResourceItemMesh>())
				{
					pso = &psoTriplanar_;
				}
				if (NowPSO != pso)
				{
					// set pipeline.
					pCmdList->GetLatestCommandList()->SetPipelineState(pso->GetPSO());
					pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					NowPSO = pso;
				}
				
				// set mesh constant.
				descSet.SetVsCbv(1, MeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);

				// set vertex buffer.
				const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
					sl12::MeshManager::CreateVertexView(meshRes->GetPositionHandle(), 0, 0, sl12::ResourceItemMesh::GetPositionStride()),
					sl12::MeshManager::CreateVertexView(meshRes->GetNormalHandle(), 0, 0, sl12::ResourceItemMesh::GetNormalStride()),
					sl12::MeshManager::CreateVertexView(meshRes->GetTangentHandle(), 0, 0, sl12::ResourceItemMesh::GetTangentStride()),
					sl12::MeshManager::CreateVertexView(meshRes->GetTexcoordHandle(), 0, 0, sl12::ResourceItemMesh::GetTexcoordStride()),
				};
				pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

				// set index buffer.
				auto ibv = sl12::MeshManager::CreateIndexView(meshRes->GetIndexHandle(), 0, 0, sl12::ResourceItemMesh::GetIndexStride());
				pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

				auto&& submeshes = meshRes->GetSubmeshes();
				auto submesh_count = submeshes.size();
				for (int i = 0; i < submesh_count; i++)
				{
					auto&& submesh = submeshes[i];
					sl12::u32 meshletCnt = (sl12::u32)submesh.meshlets.size();
					if (pso == &psoMesh_)
					{
						auto&& material = meshRes->GetMaterials()[submesh.materialIndex];
						auto bc_tex_view = GetTextureView(material.baseColorTex, device_.GetDummyTextureView(sl12::DummyTex::Black));
						auto nm_tex_view = GetTextureView(material.normalTex, device_.GetDummyTextureView(sl12::DummyTex::FlatNormal));
						auto orm_tex_view = GetTextureView(material.ormTex, device_.GetDummyTextureView(sl12::DummyTex::Black));

						descSet.SetPsSrv(0, bc_tex_view->GetDescInfo().cpuHandle);
						descSet.SetPsSrv(1, nm_tex_view->GetDescInfo().cpuHandle);
						descSet.SetPsSrv(2, orm_tex_view->GetDescInfo().cpuHandle);
					}
					else
					{
						auto dot_res = const_cast<sl12::ResourceItemTextureBase*>(hDotTex_.GetItem<sl12::ResourceItemTextureBase>());
						descSet.SetPsSrv(0, dot_res->GetTextureView().GetDescInfo().cpuHandle);
					}

					pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPsC1_, &descSet);
					pCmdList->GetLatestCommandList()->SetGraphicsRoot32BitConstant(rsVsPsC1_->GetRootConstantIndex(), (UINT)GetMaterialIndex(&meshRes->GetMaterials()[submesh.materialIndex]), 0);

#if 0
					UINT StartIndexLocation = (UINT)(submesh.indexOffsetBytes / sl12::ResourceItemMesh::GetIndexStride());
					int BaseVertexLocation = (int)(submesh.positionOffsetBytes / sl12::ResourceItemMesh::GetPositionStride());

					pCmdList->GetLatestCommandList()->DrawIndexedInstanced(submesh.indexCount, 1, StartIndexLocation, BaseVertexLocation, 0);
#else
					pCmdList->GetLatestCommandList()->ExecuteIndirect(
						meshletIndirectStandard_->GetCommandSignature(),			// command signature
						meshletCnt,													// max command count
						indirectArgBuffer_->GetResourceDep(),						// argument buffer
						meshletIndirectStandard_->GetStride() * meshletTotal + 4,	// argument buffer offset
						nullptr,													// count buffer
						0);											// count buffer offset
#endif

					meshletTotal += meshletCnt;
				}

				meshIndex++;
			}
		}
		renderGraph_->EndPass();
	}
	else
	{
		// if indirect executer is NOT existed, initialize it.
		if (!meshletIndirectVisbuffer_.IsValid())
		{
			meshletIndirectVisbuffer_ = sl12::MakeUnique<sl12::IndirectExecuter>(&device_);
			bool bIndirectExecuterSucceeded = meshletIndirectVisbuffer_->InitializeWithConstants(&device_, sl12::IndirectType::DrawIndexed, kIndirectArgsBufferStride, &rsVsPsC1_);
			assert(bIndirectExecuterSucceeded);
		}

		// create resources.
		CreateBuffers(pCmdList);

		// visibility pass.
		pTimestamp->Query(pCmdList);
		renderGraph_->NextPass(pCmdList);
		{
			GPU_MARKER(pCmdList, 0, "VisibilityPass");

			// output barrier.
			renderGraph_->BarrierOutputsAll(pCmdList);

			// clear depth.
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[3])->dsvs[0]->GetDescInfo().cpuHandle;
			pCmdList->GetLatestCommandList()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

			// set render targets.
			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
				renderGraph_->GetTarget(TargetContainer.visibilityTargetID)->rtvs[0]->GetDescInfo().cpuHandle,
			};
			pCmdList->GetLatestCommandList()->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, false, &dsv);

			// set viewport.
			D3D12_VIEWPORT vp;
			vp.TopLeftX = vp.TopLeftY = 0.0f;
			vp.Width = (float)displayWidth_;
			vp.Height = (float)displayHeight_;
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

			// set scissor rect.
			D3D12_RECT rect;
			rect.left = rect.top = 0;
			rect.right = displayWidth_;
			rect.bottom = displayHeight_;
			pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

			// set descriptors.
			sl12::DescriptorSet descSet;
			descSet.Reset();
			descSet.SetVsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetPsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoVisibility_->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// draw meshes.
			sl12::u32 meshIndex = 0;
			sl12::u32 drawCallIndex = 0;
			sl12::u32 meshletTotal = 0;
			for (auto&& mesh : sceneMeshes_)
			{
				// set mesh constant.
				descSet.SetVsCbv(1, MeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);

				auto meshRes = mesh->GetParentResource();

				// set vertex buffer.
				const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
					sl12::MeshManager::CreateVertexView(meshRes->GetPositionHandle(), 0, 0, sl12::ResourceItemMesh::GetPositionStride()),
				};
				pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

				// set index buffer.
				auto ibv = sl12::MeshManager::CreateIndexView(meshRes->GetIndexHandle(), 0, 0, sl12::ResourceItemMesh::GetIndexStride());
				pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

				pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPsC1_, &descSet);

				UINT meshletCnt = 0;
				for (auto&& submesh : meshRes->GetSubmeshes())
				{
					meshletCnt += (sl12::u32)submesh.meshlets.size();
				}
				pCmdList->GetLatestCommandList()->ExecuteIndirect(
					meshletIndirectVisbuffer_->GetCommandSignature(),			// command signature
					meshletCnt,													// max command count
					indirectArgBuffer_->GetResourceDep(),						// argument buffer
					meshletIndirectVisbuffer_->GetStride() * meshletTotal,		// argument buffer offset
					nullptr,													// count buffer
					0);											// count buffer offset

				meshletTotal += meshletCnt;

				meshIndex++;
			}
		}
		renderGraph_->EndPass();

		// material depth pass.
		pTimestamp->Query(pCmdList);
		renderGraph_->NextPass(pCmdList);
		{
			GPU_MARKER(pCmdList, 1, "MaterialDepthPass");
			
			// output barrier.
			renderGraph_->BarrierOutputsAll(pCmdList);

			D3D12_CPU_DESCRIPTOR_HANDLE dsv = renderGraph_->GetTarget(TargetContainer.matDepthTargetID)->dsvs[0]->GetDescInfo().cpuHandle;
			pCmdList->GetLatestCommandList()->OMSetRenderTargets(0, nullptr, false, &dsv);

			// set viewport.
			D3D12_VIEWPORT vp;
			vp.TopLeftX = vp.TopLeftY = 0.0f;
			vp.Width = (float)displayWidth_;
			vp.Height = (float)displayHeight_;
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

			// set scissor rect.
			D3D12_RECT rect;
			rect.left = rect.top = 0;
			rect.right = displayWidth_;
			rect.bottom = displayHeight_;
			pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

			// set descriptors.
			sl12::DescriptorSet descSet;
			descSet.Reset();
			descSet.SetPsSrv(0, renderGraph_->GetTarget(TargetContainer.visibilityTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(1, submeshBV_->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(2, meshletBV_->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(3, drawCallBV_->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(4, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoMatDepth_->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);

			// draw fullscreen.
			pCmdList->GetLatestCommandList()->DrawIndexedInstanced(3, 1, 0, 0, 0);
		}
		renderGraph_->EndPass();

		// classify pass.
		sl12::CbvHandle hTileCB;
		pTimestamp->Query(pCmdList);
		renderGraph_->NextPass(pCmdList);
		{
			UINT x = (displayWidth_ + CLASSIFY_TILE_WIDTH - 1) / CLASSIFY_TILE_WIDTH;
			UINT y = (displayHeight_ + CLASSIFY_TILE_WIDTH - 1) / CLASSIFY_TILE_WIDTH;
			TileCB cb;
			cb.numX = x;
			cb.numY = y;
			cb.tileMax = x * y;
			cb.materialMax = (sl12::u32)workMaterials_.size();
			hTileCB = cbvMan_->GetTemporal(&cb, sizeof(cb));
			
			GPU_MARKER(pCmdList, 2, "ClassifyPass");
			
			// output barrier.
			renderGraph_->BarrierOutputsAll(pCmdList);
			pCmdList->TransitionBarrier(&drawArgB_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pCmdList->TransitionBarrier(&tileIndexB_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			// set descriptors.
			sl12::DescriptorSet descSet;
			descSet.Reset();
			descSet.SetCsCbv(0, hTileCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetCsUav(0, drawArgUAV_->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoClearArg_->GetPSO());
			pCmdList->SetComputeRootSignatureAndDescriptorSet(&rsCs_, &descSet);

			// dispatch.
			UINT t = (cb.materialMax + 32 - 1) / 32;
			pCmdList->GetLatestCommandList()->Dispatch(t, 1, 1);
			
			// set descriptors.
			descSet.Reset();
			descSet.SetCsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetCsCbv(1, hTileCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(0, renderGraph_->GetTarget(TargetContainer.visibilityTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(1, submeshBV_->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(2, meshletBV_->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(3, drawCallBV_->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(4, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsUav(0, drawArgUAV_->GetDescInfo().cpuHandle);
			descSet.SetCsUav(1, tileIndexUAV_->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoClassify_->GetPSO());
			pCmdList->SetComputeRootSignatureAndDescriptorSet(&rsCs_, &descSet);

			// dispatch.
			pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);

			pCmdList->TransitionBarrier(&drawArgB_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
			pCmdList->TransitionBarrier(&tileIndexB_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		}
		renderGraph_->EndPass();

		// material tile pass.
		pTimestamp->Query(pCmdList);
		renderGraph_->NextPass(pCmdList);
		{
			GPU_MARKER(pCmdList, 1, "MaterialTilePass");
			
			// output barrier.
			renderGraph_->BarrierOutputsAll(pCmdList);

			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
				renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[0])->rtvs[0]->GetDescInfo().cpuHandle,
				renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[1])->rtvs[0]->GetDescInfo().cpuHandle,
				renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[2])->rtvs[0]->GetDescInfo().cpuHandle,
			};
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = renderGraph_->GetTarget(TargetContainer.matDepthTargetID)->dsvs[0]->GetDescInfo().cpuHandle;
			pCmdList->GetLatestCommandList()->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, false, &dsv);

			// set viewport.
			D3D12_VIEWPORT vp;
			vp.TopLeftX = vp.TopLeftY = 0.0f;
			vp.Width = (float)displayWidth_;
			vp.Height = (float)displayHeight_;
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

			// set scissor rect.
			D3D12_RECT rect;
			rect.left = rect.top = 0;
			rect.right = displayWidth_;
			rect.bottom = displayHeight_;
			pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

			// set descriptors.
			auto detail_res = const_cast<sl12::ResourceItemTextureBase*>(hDetailTex_.GetItem<sl12::ResourceItemTextureBase>());
			sl12::DescriptorSet descSet;
			descSet.Reset();
			descSet.SetVsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetVsCbv(1, hTileCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetVsSrv(0, tileIndexBV_->GetDescInfo().cpuHandle);
			descSet.SetPsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetPsCbv(1, TempCB.hDetailCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(0, renderGraph_->GetTarget(TargetContainer.visibilityTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(1, meshMan_->GetVertexBufferSRV()->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(2, meshMan_->GetIndexBufferSRV()->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(3, instanceBV_->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(4, submeshBV_->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(5, meshletBV_->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(6, drawCallBV_->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(7, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
			if (detailType_ != 3)
			{
				descSet.SetPsSrv(11, detail_res->GetTextureView().GetDescInfo().cpuHandle);
			}
			else
			{
				descSet.SetPsSrv(11, detailDerivSrv_->GetDescInfo().cpuHandle);
			}
			descSet.SetPsSampler(0, linearSampler_->GetDescInfo().cpuHandle);
			descSet.SetPsUav(0, renderGraph_->GetTarget(TargetContainer.miplevelTargetID)->uavs[0]->GetDescInfo().cpuHandle);

			sl12::GraphicsPipelineState* NowPSO = nullptr;

			sl12::u32 matIndex = 0;
			for (auto&& work : workMaterials_)
			{
				sl12::GraphicsPipelineState* pso = &psoMaterialTile_;
				if (work.psoType == 1)
				{
					pso = &psoMaterialTileTriplanar_;
				}

				if (NowPSO != pso)
				{
					// set pipeline.
					pCmdList->GetLatestCommandList()->SetPipelineState(pso->GetPSO());
					pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					NowPSO = pso;
				}
			
				MaterialTileCB cb;
				cb.materialIndex = matIndex;
				sl12::CbvHandle hMatTileCB = cbvMan_->GetTemporal(&cb, sizeof(cb));
				descSet.SetVsCbv(2, hMatTileCB.GetCBV()->GetDescInfo().cpuHandle);

				if (work.psoType == 0)
				{
					auto bc_tex_view = GetTextureView(work.pResMaterial->baseColorTex, device_.GetDummyTextureView(sl12::DummyTex::Black));
					auto nm_tex_view = GetTextureView(work.pResMaterial->normalTex, device_.GetDummyTextureView(sl12::DummyTex::FlatNormal));
					auto orm_tex_view = GetTextureView(work.pResMaterial->ormTex, device_.GetDummyTextureView(sl12::DummyTex::Black));

					descSet.SetPsSrv(8, bc_tex_view->GetDescInfo().cpuHandle);
					descSet.SetPsSrv(9, nm_tex_view->GetDescInfo().cpuHandle);
					descSet.SetPsSrv(10, orm_tex_view->GetDescInfo().cpuHandle);
				}
				else
				{
					auto dot_res = const_cast<sl12::ResourceItemTextureBase*>(hDotTex_.GetItem<sl12::ResourceItemTextureBase>());
					descSet.SetPsSrv(8, dot_res->GetTextureView().GetDescInfo().cpuHandle);
				}

				pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);

				pCmdList->GetLatestCommandList()->ExecuteIndirect(
					tileDrawIndirect_->GetCommandSignature(),	// command signature
					1,											// max command count
					drawArgB_->GetResourceDep(),				// argument buffer
					tileDrawIndirect_->GetStride() * matIndex,	// argument buffer offset
					nullptr,									// count buffer
					0);							// count buffer offset
				
				matIndex++;
			}
		}
		renderGraph_->EndPass();
	}

	// feedback miplevel pass.
	renderGraph_->NextPass(pCmdList);
	{
		GPU_MARKER(pCmdList, 1, "FeedbackMiplevelPass");

		// output barrier.
		renderGraph_->BarrierOutputsAll(pCmdList);
		pCmdList->TransitionBarrier(&miplevelBuffer_, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetCsSrv(0, renderGraph_->GetTarget(TargetContainer.miplevelTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsUav(0, miplevelUAV_->GetDescInfo().cpuHandle);

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoFeedbackMip_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rsCs_, &descSet);

		// dispatch.
		UINT w = (displayWidth_ + 3) / 4;
		UINT h = (displayHeight_ + 3) / 4;
		UINT x = (w + 7) / 8;
		UINT y = (w + 7) / 8;
		pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
	}
	renderGraph_->EndPass();

	// lighing pass.
	pTimestamp->Query(pCmdList);
	renderGraph_->NextPass(pCmdList);
	{
		GPU_MARKER(pCmdList, 1, "LightingPass");

		// output barrier.
		renderGraph_->BarrierOutputsAll(pCmdList);

		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetCsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetCsCbv(1, TempCB.hLightCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetCsCbv(2, TempCB.hShadowCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(0, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[0])->textureSrvs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(1, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[1])->textureSrvs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(2, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[2])->textureSrvs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(3, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
#if SHADOW_TYPE == 0
		descSet.SetCsSrv(4, renderGraph_->GetTarget(TargetContainer.shadowDepthTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
#else
		descSet.SetCsSrv(4, renderGraph_->GetTarget(shadowExpTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
#endif
		descSet.SetCsUav(0, renderGraph_->GetTarget(TargetContainer.accumTargetID)->uavs[0]->GetDescInfo().cpuHandle);
#if SHADOW_TYPE == 0
		descSet.SetCsSampler(0, shadowSampler_->GetDescInfo().cpuHandle);
#else
		descSet.SetCsSampler(0, linearClampSampler_->GetDescInfo().cpuHandle);
#endif

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoLighting_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rsCs_, &descSet);

		// dispatch.
		UINT x = (displayWidth_ + 7) / 8;
		UINT y = (displayHeight_ + 7) / 8;
		pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
	}
	renderGraph_->EndPass();

	// deinterleave pass.
	if (bNeedDeinterleave)
	{
		renderGraph_->NextPass(pCmdList);
		{
			GPU_MARKER(pCmdList, 1, "DeinterleavePass");

			// output barrier.
			renderGraph_->BarrierOutputsAll(pCmdList);

			// set descriptors.
			sl12::DescriptorSet descSet;
			descSet.Reset();
			descSet.SetCsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(0, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(1, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[2])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(2, renderGraph_->GetTarget(TargetContainer.accumTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsUav(0, renderGraph_->GetTarget(TargetContainer.diDepthTargetID)->uavs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsUav(1, renderGraph_->GetTarget(TargetContainer.diNormalTargetID)->uavs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsUav(2, renderGraph_->GetTarget(TargetContainer.diAccumTargetID)->uavs[0]->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoDeinterleave_->GetPSO());
			pCmdList->SetComputeRootSignatureAndDescriptorSet(&rsCs_, &descSet);

			// dispatch.
			UINT x = (displayWidth_ + 7) / 8;
			UINT y = (displayHeight_ + 7) / 8;
			pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
		}
		renderGraph_->EndPass();
	}
	
	// ssao pass.
	pTimestamp->Query(pCmdList);
	renderGraph_->NextPass(pCmdList);
	{
		GPU_MARKER(pCmdList, 1, "SSAOPass");

		// output barrier.
		renderGraph_->BarrierOutputsAll(pCmdList);

		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetCsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetCsCbv(1, TempCB.hAmbOccCB.GetCBV()->GetDescInfo().cpuHandle);
		if (bNeedDeinterleave)
		{
			descSet.SetCsSrv(0, renderGraph_->GetTarget(TargetContainer.diDepthTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(1, renderGraph_->GetTarget(TargetContainer.diNormalTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
		}
		else
		{
			descSet.SetCsSrv(0, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(1, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[2])->textureSrvs[0]->GetDescInfo().cpuHandle);
		}
		if (ssaoType_ == 2)
		{
			if (bNeedDeinterleave)
				descSet.SetCsSrv(2, renderGraph_->GetTarget(TargetContainer.diAccumTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
			else
				descSet.SetCsSrv(2, renderGraph_->GetTarget(TargetContainer.accumTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
		}

		descSet.SetCsUav(0, renderGraph_->GetTarget(TargetContainer.ssaoTargetID)->uavs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsUav(1, renderGraph_->GetTarget(TargetContainer.ssgiTargetID)->uavs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsSampler(0, linearClampSampler_->GetDescInfo().cpuHandle);

		// set pipeline.
		switch (ssaoType_)
		{
		case 0:
			pCmdList->GetLatestCommandList()->SetPipelineState(psoSsaoHbao_->GetPSO());
			break;
		case 1:
			pCmdList->GetLatestCommandList()->SetPipelineState(psoSsaoBitmask_->GetPSO());
			break;
		case 2:
			if (bNeedDeinterleave)
				pCmdList->GetLatestCommandList()->SetPipelineState(psoSsgiDI_->GetPSO());
			else
				pCmdList->GetLatestCommandList()->SetPipelineState(psoSsgi_->GetPSO());
			break;
		}
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rsCs_, &descSet);

		// dispatch.
		UINT x = (displayWidth_ + 7) / 8;
		UINT y = (displayHeight_ + 7) / 8;
		pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
	}
	renderGraph_->EndPass();

	// denoise pass.
	renderGraph_->NextPass(pCmdList);
	{
		GPU_MARKER(pCmdList, 1, "DenoisePass");

		// output barrier.
		renderGraph_->BarrierOutputsAll(pCmdList);

		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetCsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetCsCbv(1, TempCB.hAmbOccCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(0, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(1, renderGraph_->GetTarget(TargetContainer.ssaoTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
		if (depthHistory_ != sl12::kInvalidTargetID)
			descSet.SetCsSrv(2, renderGraph_->GetTarget(depthHistory_)->textureSrvs[0]->GetDescInfo().cpuHandle);
		else
			descSet.SetCsSrv(2, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
		if (ssaoHistory_ != sl12::kInvalidTargetID)
			descSet.SetCsSrv(3, renderGraph_->GetTarget(ssaoHistory_)->textureSrvs[0]->GetDescInfo().cpuHandle);
		else
			descSet.SetCsSrv(3, renderGraph_->GetTarget(TargetContainer.ssaoTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsUav(0, renderGraph_->GetTarget(TargetContainer.denoiseTargetID)->uavs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsUav(1, renderGraph_->GetTarget(TargetContainer.denoiseGITargetID)->uavs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsSampler(0, linearClampSampler_->GetDescInfo().cpuHandle);

		if (ssaoType_ == 2)
		{
			descSet.SetCsSrv(4, renderGraph_->GetTarget(TargetContainer.ssgiTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
			if (ssgiHistory_ != sl12::kInvalidTargetID)
				descSet.SetCsSrv(5, renderGraph_->GetTarget(ssgiHistory_)->textureSrvs[0]->GetDescInfo().cpuHandle);
			else
				descSet.SetCsSrv(5, renderGraph_->GetTarget(TargetContainer.ssgiTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
		}

		// set pipeline.
		if (ssaoType_ == 2)
			pCmdList->GetLatestCommandList()->SetPipelineState(psoDenoiseGI_->GetPSO());
		else
			pCmdList->GetLatestCommandList()->SetPipelineState(psoDenoise_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rsCs_, &descSet);

		// dispatch.
		UINT x = (displayWidth_ + 7) / 8;
		UINT y = (displayHeight_ + 7) / 8;
		pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
	}
	renderGraph_->EndPass();

	// indirect lighing pass.
	pTimestamp->Query(pCmdList);
	renderGraph_->NextPass(pCmdList);
	{
		GPU_MARKER(pCmdList, 1, "Indirect LightingPass");

		// output barrier.
		renderGraph_->BarrierOutputsAll(pCmdList);

		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetCsCbv(0, TempCB.hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetCsCbv(1, TempCB.hLightCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetCsCbv(2, TempCB.hDebugCB.GetCBV()->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(0, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[0])->textureSrvs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(1, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[1])->textureSrvs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(2, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[2])->textureSrvs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(3, renderGraph_->GetTarget(TargetContainer.gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(4, renderGraph_->GetTarget(TargetContainer.denoiseTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsSrv(5, renderGraph_->GetTarget(TargetContainer.denoiseGITargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
		descSet.SetCsUav(0, renderGraph_->GetTarget(TargetContainer.accumTargetID)->uavs[0]->GetDescInfo().cpuHandle);

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoIndirect_->GetPSO());
		pCmdList->SetComputeRootSignatureAndDescriptorSet(&rsCs_, &descSet);

		// dispatch.
		UINT x = (displayWidth_ + 7) / 8;
		UINT y = (displayHeight_ + 7) / 8;
		pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);
	}
	renderGraph_->EndPass();

	// tonemap pass.
	pTimestamp->Query(pCmdList);
	renderGraph_->NextPass(pCmdList);
	{
		GPU_MARKER(pCmdList, 3, "TonemapPass");

		// output barrier.
		renderGraph_->BarrierOutputsAll(pCmdList);

		// set render targets.
		auto&& rtv = swapchain.GetCurrentRenderTargetView(kSwapchainBufferOffset)->GetDescInfo().cpuHandle;
		pCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, nullptr);

		// set viewport.
		D3D12_VIEWPORT vp;
		vp.TopLeftX = vp.TopLeftY = 0.0f;
		vp.Width = (float)displayWidth_;
		vp.Height = (float)displayHeight_;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		pCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

		// set scissor rect.
		D3D12_RECT rect;
		rect.left = rect.top = 0;
		rect.right = displayWidth_;
		rect.bottom = displayHeight_;
		pCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

		// set descriptors.
		sl12::DescriptorSet descSet;
		descSet.Reset();
		descSet.SetPsSrv(0, renderGraph_->GetTarget(TargetContainer.accumTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);

		// set pipeline.
		pCmdList->GetLatestCommandList()->SetPipelineState(psoTonemap_->GetPSO());
		pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);

		// draw fullscreen.
		pCmdList->GetLatestCommandList()->DrawIndexedInstanced(3, 1, 0, 0, 0);
	}
	renderGraph_->EndPass();

	// draw GUI.
	pTimestamp->Query(pCmdList);
	gui_->LoadDrawCommands(pCmdList);

	// miplevel readback.
	if (bIsTexStreaming_)
	{
		// process copied buffer.
		std::vector<sl12::u32> mipResults;
		if (miplevelReadbacks_[1].IsValid())
		{
			mipResults.resize(miplevelReadbacks_[0]->GetBufferDesc().size / sizeof(sl12::u32));
			void* p = miplevelReadbacks_[0]->Map();
			memcpy(mipResults.data(), p, miplevelReadbacks_[0]->GetBufferDesc().size);
			miplevelReadbacks_[0]->Unmap();
			miplevelReadbacks_[0] = std::move(miplevelReadbacks_[1]);
		}

		ManageTextureStream(mipResults);

		// readback miplevel.
		UniqueHandle<sl12::Buffer> readback = sl12::MakeUnique<sl12::Buffer>(&device_);
		sl12::BufferDesc desc{};
		desc.heap = sl12::BufferHeap::ReadBack;
		desc.size = sizeof(sl12::u32) * workMaterials_.size();
		desc.usage = sl12::ResourceUsage::Unknown;
		readback->Initialize(&device_, desc);
		pCmdList->TransitionBarrier(&miplevelBuffer_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		pCmdList->GetLatestCommandList()->CopyResource(readback->GetResourceDep(), miplevelBuffer_->GetResourceDep());
		if (!miplevelReadbacks_[0].IsValid())
		{
			miplevelReadbacks_[0] = std::move(readback);
		}
		else
		{
			miplevelReadbacks_[1] = std::move(readback);
		}

	}
	
	// barrier swapchain.
	pCmdList->TransitionBarrier(swapchain.GetCurrentTexture(kSwapchainBufferOffset), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	// graphics timestamp end.
	pTimestamp->Query(pCmdList);
	pTimestamp->Resolve(pCmdList);
	timestampIndex_ = 1 - timestampIndex_;

	// wait prev frame render.
	mainCmdList_->Close();
	device_.WaitDrawDone();

	// present swapchain.
	device_.Present(1);

	// execute current frame render.
	mainCmdList_->Execute();

	// store history.
	depthHistory_ = TargetContainer.gbufferTargetIDs[3];
	ssaoHistory_ = TargetContainer.denoiseTargetID;
	ssgiHistory_ = TargetContainer.denoiseGITargetID;

	frameIndex_++;

	return true;
}

void SampleApplication::CreateMaterialList()
{
	if (!workMaterials_.empty())
	{
		return;
	}

	// list material.
	for (auto&& mesh : sceneMeshes_)
	{
		auto meshRes = mesh->GetParentResource();

		// select pso type.
		int psoType = 0;
		if (hSphereMesh_.IsValid() && hSphereMesh_.GetItem<sl12::ResourceItemMesh>() == meshRes)
		{
			psoType = 1;
		}
		
		for (auto&& mat : meshRes->GetMaterials())
		{
			auto it = std::find_if(
				workMaterials_.begin(), workMaterials_.end(),
				[&mat](const WorkMaterial& rhs) { return rhs.pResMaterial == &mat; });
			if (it == workMaterials_.end())
			{
				WorkMaterial work{};
				work.pResMaterial = &mat;
				work.psoType = psoType;

				// register textures to streamer.
				std::vector<sl12::ResourceHandle> texHandles;
				texHandles.push_back(work.pResMaterial->baseColorTex);
				texHandles.push_back(work.pResMaterial->normalTex);
				texHandles.push_back(work.pResMaterial->ormTex);
				work.texSetHandle = texStreamer_->RegisterTextureSet(texHandles);

				// add to list.
				workMaterials_.push_back(work);
			}
		}
	}

	// create miplevel feedback buffer.
	{
		sl12::BufferDesc desc{};
		desc.heap = sl12::BufferHeap::Default;
		desc.size = sizeof(sl12::u32) * workMaterials_.size();
		desc.stride = 0;
		desc.usage = sl12::ResourceUsage::UnorderedAccess;
		desc.initialState = D3D12_RESOURCE_STATE_COMMON;

		miplevelBuffer_ = sl12::MakeUnique<sl12::Buffer>(&device_);
		miplevelBuffer_->Initialize(&device_, desc);
		miplevelUAV_ = sl12::MakeUnique<sl12::UnorderedAccessView>(&device_);
		miplevelUAV_->Initialize(&device_, &miplevelBuffer_, 0, 0, 0, 0);

		desc.heap = sl12::BufferHeap::Dynamic;
		desc.usage = sl12::ResourceUsage::Unknown;
		desc.initialState = D3D12_RESOURCE_STATE_COMMON;
		miplevelCopySrc_ = sl12::MakeUnique<sl12::Buffer>(&device_);
		miplevelCopySrc_->Initialize(&device_, desc);

		void* p = miplevelCopySrc_->Map();
		memset(p, 0xff, desc.size);
		miplevelCopySrc_->Unmap();
	}

	neededMiplevels_.resize(workMaterials_.size());
	for (auto&& s : neededMiplevels_)
	{
		s.maxLevel = 0;
		s.minLevel = 0xff;
		s.prevLevel = 0xff;
		s.time = 0;
	}
	miplevelReadbacks_[0].Reset();
	miplevelReadbacks_[1].Reset();
}

void SampleApplication::CreateBuffers(sl12::CommandList* pCmdList)
{
	// count mesh data.
	std::map<const sl12::ResourceItemMesh*, sl12::u32> meshMap;
	std::vector<const sl12::ResourceItemMesh*> meshList;
	std::vector<sl12::u32> submeshOffsets;
	std::vector<sl12::u32> meshletOffsets;
	sl12::u32 meshCount = 0;
	sl12::u32 submeshCount = 0;
	sl12::u32 meshletCount = 0;
	sl12::u32 drawCallCount = 0;
	for (auto&& mesh : sceneMeshes_)
	{
		auto meshRes = mesh->GetParentResource();
		auto it = meshMap.find(meshRes);
		if (it == meshMap.end())
		{
			meshMap[meshRes] = meshCount;
			meshList.push_back(meshRes);
			submeshOffsets.push_back(submeshCount);
			meshCount++;
			submeshCount += (sl12::u32)meshRes->GetSubmeshes().size();

			for (auto&& submesh : meshRes->GetSubmeshes())
			{
				meshletOffsets.push_back(meshletCount);
				meshletCount += (sl12::u32)submesh.meshlets.size();
			}
		}

		for (auto&& submesh : meshRes->GetSubmeshes())
		{
			drawCallCount += (sl12::u32)submesh.meshlets.size();
		}
	}

	// create buffers.
	if (instanceB_.IsValid() && (instanceB_->GetBufferDesc().size < sceneMeshes_.size() * sizeof(InstanceData)))
	{
		instanceB_.Reset();
		instanceBV_.Reset();
	}
	if (submeshB_.IsValid() && (submeshB_->GetBufferDesc().size < submeshCount * sizeof(SubmeshData)))
	{
		submeshB_.Reset();
		submeshBV_.Reset();
	}
	if (meshletB_.IsValid() && (meshletB_->GetBufferDesc().size < meshletCount * sizeof(MeshletData)))
	{
		meshletB_.Reset();
		meshletBV_.Reset();
	}
	if (drawCallB_.IsValid() && (drawCallB_->GetBufferDesc().size < drawCallCount * sizeof(DrawCallData)))
	{
		drawCallB_.Reset();
		drawCallBV_.Reset();
	}
	if (!instanceB_.IsValid())
	{
		instanceB_ = sl12::MakeUnique<sl12::Buffer>(&device_);
		instanceBV_ = sl12::MakeUnique<sl12::BufferView>(&device_);

		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Default;
		desc.stride = sizeof(InstanceData);
		desc.size = sceneMeshes_.size() * desc.stride;
		desc.usage = sl12::ResourceUsage::ShaderResource;
		instanceB_->Initialize(&device_, desc);
		instanceBV_->Initialize(&device_, &instanceB_, 0, (sl12::u32)sceneMeshes_.size(), sizeof(InstanceData));
	}
	if (!submeshB_.IsValid())
	{
		submeshB_ = sl12::MakeUnique<sl12::Buffer>(&device_);
		submeshBV_ = sl12::MakeUnique<sl12::BufferView>(&device_);

		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Default;
		desc.stride = sizeof(SubmeshData);
		desc.size = submeshCount * desc.stride;
		desc.usage = sl12::ResourceUsage::ShaderResource;
		submeshB_->Initialize(&device_, desc);
		submeshBV_->Initialize(&device_, &submeshB_, 0, submeshCount, sizeof(SubmeshData));
	}
	if (!meshletB_.IsValid())
	{
		meshletB_ = sl12::MakeUnique<sl12::Buffer>(&device_);
		meshletBV_ = sl12::MakeUnique<sl12::BufferView>(&device_);

		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Default;
		desc.stride = sizeof(MeshletData);
		desc.size = meshletCount * desc.stride;
		desc.usage = sl12::ResourceUsage::ShaderResource;
		meshletB_->Initialize(&device_, desc);
		meshletBV_->Initialize(&device_, &meshletB_, 0, meshletCount, sizeof(MeshletData));
	}
	if (!drawCallB_.IsValid())
	{
		drawCallB_ = sl12::MakeUnique<sl12::Buffer>(&device_);
		drawCallBV_ = sl12::MakeUnique<sl12::BufferView>(&device_);

		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Default;
		desc.stride = sizeof(DrawCallData);
		desc.size = drawCallCount * desc.stride;
		desc.usage = sl12::ResourceUsage::ShaderResource;
		drawCallB_->Initialize(&device_, desc);
		drawCallBV_->Initialize(&device_, &drawCallB_, 0, drawCallCount, sizeof(DrawCallData));
	}

	// create copy source.
	UniqueHandle<sl12::Buffer> instanceSrc = sl12::MakeUnique<sl12::Buffer>(&device_);
	UniqueHandle<sl12::Buffer> submeshSrc = sl12::MakeUnique<sl12::Buffer>(&device_);
	UniqueHandle<sl12::Buffer> meshletSrc = sl12::MakeUnique<sl12::Buffer>(&device_);
	UniqueHandle<sl12::Buffer> drawCallSrc = sl12::MakeUnique<sl12::Buffer>(&device_);
	{
		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Dynamic;
		desc.stride = sizeof(InstanceData);
		desc.size = sceneMeshes_.size() * desc.stride;
		desc.usage = sl12::ResourceUsage::Unknown;
		instanceSrc->Initialize(&device_, desc);
	}
	{
		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Dynamic;
		desc.stride = sizeof(SubmeshData);
		desc.size = submeshCount * desc.stride;
		desc.usage = sl12::ResourceUsage::Unknown;
		submeshSrc->Initialize(&device_, desc);
	}
	{
		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Dynamic;
		desc.stride = sizeof(MeshletData);
		desc.size = meshletCount * desc.stride;
		desc.usage = sl12::ResourceUsage::Unknown;
		meshletSrc->Initialize(&device_, desc);
	}
	{
		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Dynamic;
		desc.stride = sizeof(DrawCallData);
		desc.size = drawCallCount * desc.stride;
		desc.usage = sl12::ResourceUsage::Unknown;
		drawCallSrc->Initialize(&device_, desc);
	}
	InstanceData* meshData = (InstanceData*)instanceSrc->Map();
	SubmeshData* submeshData = (SubmeshData*)submeshSrc->Map();
	MeshletData* meshletData = (MeshletData*)meshletSrc->Map();
	DrawCallData* drawCallData = (DrawCallData*)drawCallSrc->Map();

	// fill source.
	sl12::u32 submeshTotal = 0;
	for (auto meshRes : meshList)
	{
		// select pso type.
		int psoType = 0;
		if (hSphereMesh_.IsValid() && hSphereMesh_.GetItem<sl12::ResourceItemMesh>() == meshRes)
		{
			psoType = 1;
		}
		
		auto&& submeshes = meshRes->GetSubmeshes();
		for (auto&& submesh : submeshes)
		{
			sl12::u32 submeshIndexOffset = (sl12::u32)(meshRes->GetIndexHandle().offset + submesh.indexOffsetBytes);
			submeshData->materialIndex = GetMaterialIndex(&meshRes->GetMaterials()[submesh.materialIndex]);
			submeshData->posOffset = (sl12::u32)(meshRes->GetPositionHandle().offset + submesh.positionOffsetBytes);
			submeshData->normalOffset = (sl12::u32)(meshRes->GetNormalHandle().offset + submesh.normalOffsetBytes);
			submeshData->tangentOffset = (sl12::u32)(meshRes->GetTangentHandle().offset + submesh.tangentOffsetBytes);
			submeshData->uvOffset = (sl12::u32)(meshRes->GetTexcoordHandle().offset + submesh.texcoordOffsetBytes);
			submeshData->indexOffset = submeshIndexOffset;
			submeshData++;

			for (auto&& meshlet : submesh.meshlets)
			{
				meshletData->submeshIndex = submeshTotal;
				meshletData->indexOffset = submeshIndexOffset + meshlet.indexOffset * (sl12::u32)sl12::ResourceItemMesh::GetIndexStride();
				meshletData++;
			}

			submeshTotal++;
		}
	}
	sl12::u32 instanceIndex = 0;
	for (auto&& mesh : sceneMeshes_)
	{
		auto meshRes = mesh->GetParentResource();
		auto meshIndex = meshMap[meshRes];
		auto submeshOffset = submeshOffsets[meshIndex];
		
		// set mesh constant.
		DirectX::XMMATRIX l2w = DirectX::XMLoadFloat4x4(&mesh->GetMtxLocalToWorld());
		DirectX::XMMATRIX w2l = DirectX::XMMatrixInverse(nullptr, l2w);
		meshData->mtxLocalToWorld = mesh->GetMtxLocalToWorld();
		DirectX::XMStoreFloat4x4(&meshData->mtxWorldToLocal, w2l);
		meshData++;

		auto&& submeshes = meshRes->GetSubmeshes();
		sl12::u32 submesh_count = (sl12::u32)submeshes.size();
		for (sl12::u32 i = 0; i < submesh_count; i++)
		{
			auto meshletOffset = meshletOffsets[submeshOffset + i];
			sl12::u32 meshlet_count = (sl12::u32)submeshes[i].meshlets.size();
			for (sl12::u32 j = 0; j < meshlet_count; j++)
			{
				drawCallData->instanceIndex = instanceIndex;
				drawCallData->meshletIndex = meshletOffset + j;
				drawCallData++;
			}
		}

		instanceIndex++;
	}
	instanceSrc->Unmap();
	submeshSrc->Unmap();
	meshletSrc->Unmap();
	drawCallSrc->Unmap();

	// create draw args.
	UINT tileXCount = (displayWidth_ + CLASSIFY_TILE_WIDTH - 1) / CLASSIFY_TILE_WIDTH;
	UINT tileYCount = (displayHeight_ + CLASSIFY_TILE_WIDTH - 1) / CLASSIFY_TILE_WIDTH;
	UINT tileMax = tileXCount * tileYCount;
	if (drawArgB_.IsValid() && (drawArgB_->GetBufferDesc().size < tileDrawIndirect_->GetStride() * workMaterials_.size()))
	{
		drawArgB_.Reset();
		drawArgUAV_.Reset();
	}
	if (tileIndexB_.IsValid() && (tileIndexB_->GetBufferDesc().size < sizeof(sl12::u32) * tileMax * workMaterials_.size()))
	{
		tileIndexB_.Reset();
		tileIndexBV_.Reset();
		tileIndexUAV_.Reset();
	}
	if (!drawArgB_.IsValid())
	{
		drawArgB_ = sl12::MakeUnique<sl12::Buffer>(&device_);
		drawArgUAV_ = sl12::MakeUnique<sl12::UnorderedAccessView>(&device_);

		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Default;
		desc.size = tileDrawIndirect_->GetStride() * workMaterials_.size();
		desc.stride = 0;
		desc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess;
		drawArgB_->Initialize(&device_, desc);
		drawArgUAV_->Initialize(&device_, &drawArgB_, 0, (sl12::u32)desc.size / sizeof(sl12::u32), 0, 0);
	}
	if (!tileIndexB_.IsValid())
	{
		tileIndexB_ = sl12::MakeUnique<sl12::Buffer>(&device_);
		tileIndexBV_ = sl12::MakeUnique<sl12::BufferView>(&device_);
		tileIndexUAV_ = sl12::MakeUnique<sl12::UnorderedAccessView>(&device_);

		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Default;
		desc.size = sizeof(sl12::u32) * tileMax * workMaterials_.size();
		desc.stride = 0;
		desc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess;
		tileIndexB_->Initialize(&device_, desc);
		tileIndexBV_->Initialize(&device_, &tileIndexB_, 0, 0, 0);
		tileIndexUAV_->Initialize(&device_, &tileIndexB_, 0, 0, 0, 0);
	}

	// copy commands.
	pCmdList->TransitionBarrier(&instanceB_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
	pCmdList->TransitionBarrier(&submeshB_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
	pCmdList->TransitionBarrier(&meshletB_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
	pCmdList->TransitionBarrier(&drawCallB_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
	pCmdList->GetLatestCommandList()->CopyBufferRegion(instanceB_->GetResourceDep(), 0, instanceSrc->GetResourceDep(), 0, instanceSrc->GetBufferDesc().size);
	pCmdList->GetLatestCommandList()->CopyBufferRegion(submeshB_->GetResourceDep(), 0, submeshSrc->GetResourceDep(), 0, submeshSrc->GetBufferDesc().size);
	pCmdList->GetLatestCommandList()->CopyBufferRegion(meshletB_->GetResourceDep(), 0, meshletSrc->GetResourceDep(), 0, meshletSrc->GetBufferDesc().size);
	pCmdList->GetLatestCommandList()->CopyBufferRegion(drawCallB_->GetResourceDep(), 0, drawCallSrc->GetResourceDep(), 0, drawCallSrc->GetBufferDesc().size);
	pCmdList->TransitionBarrier(&instanceB_, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	pCmdList->TransitionBarrier(&submeshB_, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	pCmdList->TransitionBarrier(&meshletB_, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	pCmdList->TransitionBarrier(&drawCallB_, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void SampleApplication::CreateMeshletBounds(sl12::CommandList* pCmdList)
{
	auto CreateAndCopy = [&](sl12::ResourceHandle hRes, UniqueHandle<sl12::Buffer>& B, UniqueHandle<sl12::BufferView>& BV)
	{
		auto resMesh = hRes.GetItem<sl12::ResourceItemMesh>();
		auto&& submeshes = resMesh->GetSubmeshes();

		// count meshlets.
		sl12::u32 meshletTotal = 0;
		for (auto&& submesh : submeshes)
		{
			meshletTotal += (sl12::u32)submesh.meshlets.size();
		}

		// create buffers.
		B = sl12::MakeUnique<sl12::Buffer>(&device_);
		BV = sl12::MakeUnique<sl12::BufferView>(&device_);
		UniqueHandle<sl12::Buffer> UploadB = sl12::MakeUnique<sl12::Buffer>(&device_);
		
		sl12::BufferDesc desc{};
		desc.heap = sl12::BufferHeap::Default;
		desc.stride = sizeof(MeshletBound);
		desc.size = desc.stride * meshletTotal;
		desc.usage = sl12::ResourceUsage::ShaderResource;
		desc.initialState = D3D12_RESOURCE_STATE_COMMON;

		B->Initialize(&device_, desc);
		BV->Initialize(&device_, &B, 0, 0, (sl12::u32)desc.stride);

		desc.heap = sl12::BufferHeap::Dynamic;
		UploadB->Initialize(&device_, desc);

		// fill upload buffer.
		MeshletBound* pBound = static_cast<MeshletBound*>(UploadB->Map());
		for (auto&& submesh : submeshes)
		{
			for (auto&& meshlet : submesh.meshlets)
			{
				pBound->aabbMin = meshlet.boundingInfo.box.aabbMin;
				pBound->aabbMax = meshlet.boundingInfo.box.aabbMax;
				pBound->coneAxis = meshlet.boundingInfo.cone.axis;
				pBound->coneApex = meshlet.boundingInfo.cone.apex;
				pBound->coneCutoff = meshlet.boundingInfo.cone.cutoff;
				pBound++;
			}
		}
		UploadB->Unmap();

		// copy buffer.
		pCmdList->GetLatestCommandList()->CopyResource(B->GetResourceDep(), UploadB->GetResourceDep());
		pCmdList->TransitionBarrier(&B, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	};

	if (hSuzanneMesh_.IsValid())
	{
		CreateAndCopy(hSuzanneMesh_, SuzanneMeshletB_, SuzanneMeshletBV_);
	}
	if (hSponzaMesh_.IsValid())
	{
		CreateAndCopy(hSponzaMesh_, SponzaMeshletB_, SponzaMeshletBV_);
	}
	if (hCurtainMesh_.IsValid())
	{
		CreateAndCopy(hCurtainMesh_, CurtainMeshletB_, CurtainMeshletBV_);
	}
	if (hSphereMesh_.IsValid())
	{
		CreateAndCopy(hSphereMesh_, SphereMeshletB_, SphereMeshletBV_);
	}
}

int SampleApplication::Input(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_LBUTTONDOWN:
		inputData_.mouseButton |= sl12::MouseButton::Left;
		return 0;
	case WM_RBUTTONDOWN:
		inputData_.mouseButton |= sl12::MouseButton::Right;
		return 0;
	case WM_MBUTTONDOWN:
		inputData_.mouseButton |= sl12::MouseButton::Middle;
		return 0;
	case WM_LBUTTONUP:
		inputData_.mouseButton &= ~sl12::MouseButton::Left;
		return 0;
	case WM_RBUTTONUP:
		inputData_.mouseButton &= ~sl12::MouseButton::Right;
		return 0;
	case WM_MBUTTONUP:
		inputData_.mouseButton &= ~sl12::MouseButton::Middle;
		return 0;
	case WM_MOUSEMOVE:
		inputData_.mouseX = GET_X_LPARAM(lParam);
		inputData_.mouseY = GET_Y_LPARAM(lParam);
		return 0;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		inputData_.key = wParam;
		inputData_.scancode = (int)LOBYTE(HIWORD(lParam));;
		inputData_.keyDown = false;
		return 0;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		inputData_.key = wParam;
		inputData_.scancode = (int)LOBYTE(HIWORD(lParam));;
		inputData_.keyDown = true;
		return 0;
	case WM_CHAR:
		inputData_.chara = (sl12::u16)wParam;
		return 0;
	}

	return 0;
}

void SampleApplication::ControlCamera(float deltaTime)
{
	const float kCameraMoveSpeed = 300.0f;
	const float kCameraRotSpeed = 10.0f;
	float x = 0.0f, y = 0.0f, z = 0.0f;
	float rx = 0.0f, ry = 0.0f;
	if (GetKeyState('W') < 0)
	{
		z = 1.0f;
	}
	else if (GetKeyState('S') < 0)
	{
		z = -1.0f;
	}
	if (GetKeyState('A') < 0)
	{
		x = -1.0f;
	}
	else if (GetKeyState('D') < 0)
	{
		x = 1.0f;
	}
	if (GetKeyState('Q') < 0)
	{
		y = -1.0f;
	}
	else if (GetKeyState('E') < 0)
	{
		y = 1.0f;
	}

	if (inputData_.mouseButton & sl12::MouseButton::Right)
	{
		rx = -(float)(inputData_.mouseY - lastMouseY_);
		ry = -(float)(inputData_.mouseX - lastMouseX_);
	}
	lastMouseX_ = inputData_.mouseX;
	lastMouseY_ = inputData_.mouseY;

	DirectX::XMFLOAT3 upVec(0.0f, 1.0f, 0.0f);
	auto cp = DirectX::XMLoadFloat3(&cameraPos_);
	auto c_forward = DirectX::XMLoadFloat3(&cameraDir_);
	auto c_right = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(c_forward, DirectX::XMLoadFloat3(&upVec)));
	auto mtxRot = DirectX::XMMatrixMultiply(DirectX::XMMatrixRotationAxis(c_right, DirectX::XMConvertToRadians(rx * kCameraRotSpeed) * deltaTime), DirectX::XMMatrixRotationY(DirectX::XMConvertToRadians(ry * kCameraRotSpeed) * deltaTime));
	c_forward = DirectX::XMVector4Transform(c_forward, mtxRot);
	cp = DirectX::XMVectorAdd(cp, DirectX::XMVectorScale(c_forward, z * kCameraMoveSpeed * deltaTime));
	cp = DirectX::XMVectorAdd(cp, DirectX::XMVectorScale(c_right, x * kCameraMoveSpeed * deltaTime));
	cp = DirectX::XMVectorAdd(cp, DirectX::XMVectorSet(0.0f, y * kCameraMoveSpeed * deltaTime, 0.0f, 0.0f));
	DirectX::XMStoreFloat3(&cameraPos_, cp);
	DirectX::XMStoreFloat3(&cameraDir_, c_forward);
}

void SampleApplication::ComputeSceneAABB()
{
	DirectX::XMFLOAT3 aabbMax(-FLT_MAX, -FLT_MAX, -FLT_MAX), aabbMin(FLT_MAX, FLT_MAX, FLT_MAX);
	for (auto&& mesh : sceneMeshes_)
	{
		auto mtx = DirectX::XMLoadFloat4x4(&mesh->GetMtxLocalToWorld());
		auto&& bound = mesh->GetParentResource()->GetBoundingInfo();
		DirectX::XMFLOAT3 pnts[] = {
			DirectX::XMFLOAT3(bound.box.aabbMax.x, bound.box.aabbMax.y, bound.box.aabbMax.z),
			DirectX::XMFLOAT3(bound.box.aabbMax.x, bound.box.aabbMax.y, bound.box.aabbMin.z),
			DirectX::XMFLOAT3(bound.box.aabbMax.x, bound.box.aabbMin.y, bound.box.aabbMax.z),
			DirectX::XMFLOAT3(bound.box.aabbMax.x, bound.box.aabbMin.y, bound.box.aabbMin.z),
			DirectX::XMFLOAT3(bound.box.aabbMin.x, bound.box.aabbMax.y, bound.box.aabbMax.z),
			DirectX::XMFLOAT3(bound.box.aabbMin.x, bound.box.aabbMax.y, bound.box.aabbMin.z),
			DirectX::XMFLOAT3(bound.box.aabbMin.x, bound.box.aabbMin.y, bound.box.aabbMax.z),
			DirectX::XMFLOAT3(bound.box.aabbMin.x, bound.box.aabbMin.y, bound.box.aabbMin.z),
		};
		for (auto pnt : pnts)
		{
			auto p = DirectX::XMLoadFloat3(&pnt);
			p = DirectX::XMVector3TransformCoord(p, mtx);
			DirectX::XMStoreFloat3(&pnt, p);

			aabbMax.x = std::max(pnt.x, aabbMax.x);
			aabbMax.y = std::max(pnt.y, aabbMax.y);
			aabbMax.z = std::max(pnt.z, aabbMax.z);
			aabbMin.x = std::min(pnt.x, aabbMin.x);
			aabbMin.y = std::min(pnt.y, aabbMin.y);
			aabbMin.z = std::min(pnt.z, aabbMin.z);
		}
	}
	sceneAABBMax_ = aabbMax;
	sceneAABBMin_ = aabbMin;
}

void SampleApplication::ManageTextureStream(const std::vector<sl12::u32>& miplevels)
{
	// process needed levels.
	if (!miplevels.empty())
	{
		auto p = miplevels.data();
		for (auto&& s : neededMiplevels_)
		{
			sl12::u32 minL = std::min(*p, s.minLevel);
			sl12::u32 maxL = std::max(*p, s.maxLevel);
			p++;

			s.minLevel = minL;
			s.maxLevel = maxL;
			if (s.prevLevel == s.minLevel)
			{
				s.minLevel = 0xff;
				s.time = 0;
			}
			else
			{
				s.time++;
			}
		}
	}

	// request texture streaming.
	int i = 0;
	for (auto&& s : neededMiplevels_)
	{
		// if 30 frames elapsed.
		if (s.time >= 30)
		{
			sl12::u32 targetWidth = std::max(4096u >> s.minLevel, 1u);
			texStreamer_->RequestStreaming(workMaterials_[i].texSetHandle, targetWidth);
			if (s.prevLevel != 0xff && s.prevLevel < s.minLevel)
			{
				s.prevLevel--;
			}
			else
			{
				s.prevLevel = s.minLevel;
			}
			s.maxLevel = 0;
			s.minLevel = 0xff;
			s.time = 0;
		}
		i++;
	}
}


//	EOF
