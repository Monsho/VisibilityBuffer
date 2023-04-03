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

namespace
{
	static const char* kResourceDir = "resources";
	static const char* kShaderDir = "VisibilityBuffer/shaders";
	static const char* kShaderIncludeDir = "../SampleLib12/SampleLib12/shaders/include";

	static std::vector<sl12::RenderGraphTargetDesc> gGBufferDescs;
	static sl12::RenderGraphTargetDesc gAccumDesc;
	static sl12::RenderGraphTargetDesc gVisibilityDesc;
	static sl12::RenderGraphTargetDesc gMatDepthDesc;
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

	// initialize shader manager.
	std::vector<std::string> shaderIncludeDirs;
	shaderIncludeDirs.push_back(sl12::JoinPath(homeDir_, kShaderIncludeDir));
	shaderMan_ = sl12::MakeUnique<sl12::ShaderManager>(nullptr);
	if (!shaderMan_->Initialize(&device_, &shaderIncludeDirs))
	{
		sl12::ConsolePrint("Error: failed to init shader manager.");
		return false;
	}

	// compile shaders.
	const std::string shaderBaseDir = sl12::JoinPath(homeDir_, kShaderDir);
	hMeshVV_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "mesh.vv.hlsl"),
		"main", sl12::ShaderType::Vertex, 6, 5, nullptr, nullptr);
	hMeshP_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "mesh.p.hlsl"),
		"main", sl12::ShaderType::Pixel, 6, 5, nullptr, nullptr);
	hVisibilityVV_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "visibility.vv.hlsl"),
		"main", sl12::ShaderType::Vertex, 6, 5, nullptr, nullptr);
	hVisibilityP_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "visibility.p.hlsl"),
		"main", sl12::ShaderType::Pixel, 6, 5, nullptr, nullptr);
	hLightingC_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "lighting.c.hlsl"),
		"main", sl12::ShaderType::Compute, 6, 5, nullptr, nullptr);
	hFullscreenVV_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "fullscreen.vv.hlsl"),
		"main", sl12::ShaderType::Vertex, 6, 5, nullptr, nullptr);
	hTonemapP_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "tonemap.p.hlsl"),
		"main", sl12::ShaderType::Pixel, 6, 5, nullptr, nullptr);
	hClassifyC_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "classify.c.hlsl"),
		"main", sl12::ShaderType::Compute, 6, 5, nullptr, nullptr);
	hMatDepthP_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "material_depth.p.hlsl"),
		"main", sl12::ShaderType::Pixel, 6, 5, nullptr, nullptr);
	hClearArgC_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "clear_arg.c.hlsl"),
		"main", sl12::ShaderType::Compute, 6, 5, nullptr, nullptr);
	hMaterialTileVV_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "material_tile.vv.hlsl"),
		"main", sl12::ShaderType::Vertex, 6, 5, nullptr, nullptr);
	hMaterialTileP_ = shaderMan_->CompileFromFile(
		sl12::JoinPath(shaderBaseDir, "material_tile.p.hlsl"),
		"main", sl12::ShaderType::Pixel, 6, 5, nullptr, nullptr);
	
	// load request.
	if (meshType_ == 0)
	{
		hSuzanneMesh_ = resLoader_->LoadRequest<sl12::ResourceItemMesh>("mesh/hp_suzanne/hp_suzanne.rmesh");
	}
	else
	{
		hSponzaMesh_ = resLoader_->LoadRequest<sl12::ResourceItemMesh>("mesh/sponza/sponza.rmesh");
	}

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

		D3D12_SAMPLER_DESC desc{};
		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.MaxLOD = FLT_MAX;
		desc.MinLOD = 0.0f;
		desc.MipLODBias = 0.0f;
		linearSampler_->Initialize(&device_, desc);
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

	// execute utility commands.
	utilCmdList->Close();
	utilCmdList->Execute();
	device_.WaitDrawDone();

	// wait compile and load.
	while (shaderMan_->IsCompiling() || resLoader_->IsLoading())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	
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
	else
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
	
	// init root signature and pipeline state.
	rsVsPs_ = sl12::MakeUnique<sl12::RootSignature>(&device_);
	psoMesh_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoVisibility_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoTonemap_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoMatDepth_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	psoMaterialTile_ = sl12::MakeUnique<sl12::GraphicsPipelineState>(&device_);
	rsVsPs_->Initialize(&device_, hMeshVV_.GetShader(), hMeshP_.GetShader(), nullptr, nullptr, nullptr);
	{
		sl12::GraphicsPipelineStateDesc desc{};
		desc.pRootSignature = &rsVsPs_;
		desc.pVS = hMeshVV_.GetShader();
		desc.pPS = hMeshP_.GetShader();

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_BACK;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

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
		desc.pRootSignature = &rsVsPs_;
		desc.pVS = hVisibilityVV_.GetShader();
		desc.pPS = hVisibilityP_.GetShader();

		desc.blend.sampleMask = UINT_MAX;
		desc.blend.rtDesc[0].isBlendEnable = false;
		desc.blend.rtDesc[0].writeMask = 0xf;

		desc.rasterizer.cullMode = D3D12_CULL_MODE_BACK;
		desc.rasterizer.fillMode = D3D12_FILL_MODE_SOLID;
		desc.rasterizer.isDepthClipEnable = true;
		desc.rasterizer.isFrontCCW = true;

		desc.depthStencil.isDepthEnable = true;
		desc.depthStencil.isDepthWriteEnable = true;
		desc.depthStencil.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

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
		desc.pVS = hFullscreenVV_.GetShader();
		desc.pPS = hTonemapP_.GetShader();

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
		desc.pVS = hFullscreenVV_.GetShader();
		desc.pPS = hMatDepthP_.GetShader();

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
		desc.pVS = hMaterialTileVV_.GetShader();
		desc.pPS = hMaterialTileP_.GetShader();

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
	}

	rsCs_ = sl12::MakeUnique<sl12::RootSignature>(&device_);
	psoLighting_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoClassify_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	psoClearArg_ = sl12::MakeUnique<sl12::ComputePipelineState>(&device_);
	rsCs_->Initialize(&device_, hLightingC_.GetShader());
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hLightingC_.GetShader();

		if (!psoLighting_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init lighting pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hClassifyC_.GetShader();

		if (!psoClassify_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init classify pso.");
			return false;
		}
	}
	{
		sl12::ComputePipelineStateDesc desc{};
		desc.pRootSignature = &rsCs_;
		desc.pCS = hClearArgC_.GetShader();

		if (!psoClearArg_->Initialize(&device_, desc))
		{
			sl12::ConsolePrint("Error: failed to init clear arg pso.");
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
	for (auto&& t : timestamps_) t.Destroy();
	tileDrawIndirect_.Reset();
	gui_.Reset();
	psoLighting_.Reset();
	psoClearArg_.Reset();
	psoClassify_.Reset();
	psoMaterialTile_.Reset();
	psoTonemap_.Reset();
	psoMatDepth_.Reset();
	psoVisibility_.Reset();
	psoMesh_.Reset();
	rsCs_.Reset();
	rsVsPs_.Reset();
	renderGraph_.Reset();
	cbvMan_.Reset();
	mainCmdList_.Reset();
	shaderMan_.Reset();
	resLoader_.Reset();
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

		if (ImGui::Checkbox("Visibility Buffer", &bEnableVisibilityBuffer_))
		{}
		
		uint64_t freq = device_.GetGraphicsQueue().GetTimestampFrequency();
		if (!bPrevMode)
		{
			uint64_t timestamp[6];

			pTimestamp->GetTimestamp(0, 6, timestamp);
			uint64_t total = timestamp[5] - timestamp[0];
			uint64_t gbuffer = timestamp[2] - timestamp[1];
			uint64_t lighting = timestamp[3] - timestamp[2];
			uint64_t tonemap = timestamp[4] - timestamp[3];

			ImGui::Text("GPU Time");
			ImGui::Text("  Total   : %f (ms)", (float)total / ((float)freq / 1000.0f));
			ImGui::Text("  GBuffer : %f (ms)", (float)gbuffer / ((float)freq / 1000.0f));
			ImGui::Text("  Lighting: %f (ms)", (float)lighting / ((float)freq / 1000.0f));
			ImGui::Text("  Tonemap : %f (ms)", (float)tonemap / ((float)freq / 1000.0f));
		}
		else
		{
			uint64_t timestamp[9];

			pTimestamp->GetTimestamp(0, 9, timestamp);
			uint64_t total = timestamp[8] - timestamp[0];
			uint64_t visibility = timestamp[2] - timestamp[1];
			uint64_t depth = timestamp[3] - timestamp[2];
			uint64_t classify = timestamp[4] - timestamp[3];
			uint64_t tile = timestamp[5] - timestamp[4];
			uint64_t lighting = timestamp[6] - timestamp[5];
			uint64_t tonemap = timestamp[7] - timestamp[6];

			ImGui::Text("GPU Time");
			ImGui::Text("  Total      : %f (ms)", (float)total / ((float)freq / 1000.0f));
			ImGui::Text("  Visibility : %f (ms)", (float)visibility / ((float)freq / 1000.0f));
			ImGui::Text("  Depth      : %f (ms)", (float)depth / ((float)freq / 1000.0f));
			ImGui::Text("  Classify   : %f (ms)", (float)classify / ((float)freq / 1000.0f));
			ImGui::Text("  MatTile    : %f (ms)", (float)tile / ((float)freq / 1000.0f));
			ImGui::Text("  Lighting   : %f (ms)", (float)lighting / ((float)freq / 1000.0f));
			ImGui::Text("  Tonemap    : %f (ms)", (float)tonemap / ((float)freq / 1000.0f));
		}
	}
	ImGui::Render();

	device_.WaitPresent();
	device_.SyncKillObjects();

	pTimestamp->Reset();
	pTimestamp->Query(pCmdList);
	device_.LoadRenderCommands(pCmdList);
	meshMan_->BeginNewFrame(pCmdList);
	cbvMan_->BeginNewFrame();
	renderGraph_->BeginNewFrame();

	// create targets.
	std::vector<sl12::RenderGraphTargetID> gbufferTargetIDs;
	sl12::RenderGraphTargetID accumTargetID;
	sl12::RenderGraphTargetID visibilityTargetID;
	sl12::RenderGraphTargetID matDepthTargetID;
	for (auto&& desc : gGBufferDescs)
	{
		gbufferTargetIDs.push_back(renderGraph_->AddTarget(desc));
	}
	accumTargetID = renderGraph_->AddTarget(gAccumDesc);
	visibilityTargetID = renderGraph_->AddTarget(gVisibilityDesc);
	matDepthTargetID = renderGraph_->AddTarget(gMatDepthDesc);

	// create render passes.
	{
		std::vector<sl12::RenderPass> passes;
		std::vector<sl12::RenderGraphTargetID> histories;

		if (!bEnableVisibilityBuffer_)
		{
			sl12::RenderPass gbufferPass{};
			gbufferPass.output.push_back(gbufferTargetIDs[0]);
			gbufferPass.output.push_back(gbufferTargetIDs[1]);
			gbufferPass.output.push_back(gbufferTargetIDs[2]);
			gbufferPass.output.push_back(gbufferTargetIDs[3]);
			gbufferPass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
			gbufferPass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
			gbufferPass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
			gbufferPass.outputStates.push_back(D3D12_RESOURCE_STATE_DEPTH_WRITE);
			passes.push_back(gbufferPass);

			sl12::RenderPass lightingPass{};
			lightingPass.input.push_back(gbufferTargetIDs[0]);
			lightingPass.input.push_back(gbufferTargetIDs[1]);
			lightingPass.input.push_back(gbufferTargetIDs[2]);
			lightingPass.input.push_back(gbufferTargetIDs[3]);
			lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			lightingPass.output.push_back(accumTargetID);
			lightingPass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			passes.push_back(lightingPass);

			sl12::RenderPass tonemapPass{};
			tonemapPass.input.push_back(accumTargetID);
			tonemapPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			passes.push_back(tonemapPass);
		}
		else
		{
			sl12::RenderPass visibilityPass{};
			visibilityPass.output.push_back(visibilityTargetID);
			visibilityPass.output.push_back(gbufferTargetIDs[3]);
			visibilityPass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
			visibilityPass.outputStates.push_back(D3D12_RESOURCE_STATE_DEPTH_WRITE);
			passes.push_back(visibilityPass);

			sl12::RenderPass matDepthPass{};
			matDepthPass.input.push_back(visibilityTargetID);
			matDepthPass.input.push_back(gbufferTargetIDs[3]);
			matDepthPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			matDepthPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			matDepthPass.output.push_back(matDepthTargetID);
			matDepthPass.output.push_back(accumTargetID);
			matDepthPass.outputStates.push_back(D3D12_RESOURCE_STATE_DEPTH_WRITE);
			matDepthPass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			passes.push_back(matDepthPass);
			
			sl12::RenderPass classifyPass{};
			classifyPass.input.push_back(visibilityTargetID);
			classifyPass.input.push_back(gbufferTargetIDs[3]);
			classifyPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			classifyPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			passes.push_back(classifyPass);
			
			sl12::RenderPass matTilePass{};
			matTilePass.input.push_back(visibilityTargetID);
			matTilePass.input.push_back(gbufferTargetIDs[3]);
			matTilePass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			matTilePass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			matTilePass.output.push_back(gbufferTargetIDs[0]);
			matTilePass.output.push_back(gbufferTargetIDs[1]);
			matTilePass.output.push_back(gbufferTargetIDs[2]);
			matTilePass.output.push_back(gbufferTargetIDs[3]);
			matTilePass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
			matTilePass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
			matTilePass.outputStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);
			matTilePass.outputStates.push_back(D3D12_RESOURCE_STATE_DEPTH_WRITE);
			passes.push_back(matTilePass);
			
			sl12::RenderPass lightingPass{};
			lightingPass.input.push_back(gbufferTargetIDs[0]);
			lightingPass.input.push_back(gbufferTargetIDs[1]);
			lightingPass.input.push_back(gbufferTargetIDs[2]);
			lightingPass.input.push_back(gbufferTargetIDs[3]);
			lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			lightingPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			lightingPass.output.push_back(accumTargetID);
			lightingPass.outputStates.push_back(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			passes.push_back(lightingPass);

			sl12::RenderPass tonemapPass{};
			tonemapPass.input.push_back(accumTargetID);
			tonemapPass.inputStates.push_back(D3D12_RESOURCE_STATE_GENERIC_READ);
			passes.push_back(tonemapPass);
		}

		renderGraph_->CreateRenderPasses(&device_, passes, histories);
	}

	// create scene constant buffer.
	sl12::CbvHandle hSceneCB;
	{
		DirectX::XMFLOAT3 camPos(1000.0f, 1000.0f, 0.0f);
		DirectX::XMFLOAT3 tgtPos(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT3 upVec(0.0f, 1.0f, 0.0f);
		auto cp = DirectX::XMLoadFloat3(&cameraPos_);
		auto dir = DirectX::XMLoadFloat3(&cameraDir_);
		auto up = DirectX::XMLoadFloat3(&upVec);
		auto mtxWorldToView = DirectX::XMMatrixLookAtRH(cp, DirectX::XMVectorAdd(cp, dir), up);
		auto mtxViewToClip = sl12::MatrixPerspectiveInfiniteFovRH(DirectX::XMConvertToRadians(60.0f), (float)displayWidth_ / (float)displayHeight_, 0.1f);
		auto mtxWorldToClip = mtxWorldToView * mtxViewToClip;
		auto mtxClipToWorld = DirectX::XMMatrixInverse(nullptr, mtxWorldToClip);
		auto mtxViewToWorld = DirectX::XMMatrixInverse(nullptr, mtxWorldToView);

		SceneCB cbScene;
		DirectX::XMStoreFloat4x4(&cbScene.mtxWorldToProj, mtxWorldToClip);
		DirectX::XMStoreFloat4x4(&cbScene.mtxWorldToView, mtxWorldToView);
		DirectX::XMStoreFloat4x4(&cbScene.mtxProjToWorld, mtxClipToWorld);
		DirectX::XMStoreFloat4x4(&cbScene.mtxViewToWorld, mtxViewToWorld);
		cbScene.eyePosition.x = camPos.x;
		cbScene.eyePosition.y = camPos.y;
		cbScene.eyePosition.z = camPos.z;
		cbScene.eyePosition.w = 0.0f;
		cbScene.screenSize.x = (float)displayWidth_;
		cbScene.screenSize.y = (float)displayHeight_;

		hSceneCB = cbvMan_->GetTemporal(&cbScene, sizeof(cbScene));
	}

	// clear swapchain.
	auto&& swapchain = device_.GetSwapchain();
	pCmdList->TransitionBarrier(swapchain.GetCurrentTexture(kSwapchainBufferOffset), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	{
		float color[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
		pCmdList->GetLatestCommandList()->ClearRenderTargetView(swapchain.GetCurrentRenderTargetView(kSwapchainBufferOffset)->GetDescInfo().cpuHandle, color, 0, nullptr);
	}

	if (!bEnableVisibilityBuffer_)
	{
		// gbuffer pass.
		pTimestamp->Query(pCmdList);
		renderGraph_->BeginPass(pCmdList, 0);
		{
			std::vector<sl12::CbvHandle> MeshCBs;
			for (auto&& mesh : sceneMeshes_)
			{
				// set mesh constant.
				MeshCB cbMesh;
				cbMesh.mtxLocalToWorld = mesh->GetMtxLocalToWorld();
				cbMesh.mtxPrevLocalToWorld = mesh->GetMtxPrevLocalToWorld();
				MeshCBs.push_back(cbvMan_->GetTemporal(&cbMesh, sizeof(cbMesh)));
			}

			GPU_MARKER(pCmdList, 0, "GBufferPass");

			// output barrier.
			renderGraph_->BarrierOutputsAll(pCmdList);

			// clear depth.
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = renderGraph_->GetTarget(gbufferTargetIDs[3])->dsvs[0]->GetDescInfo().cpuHandle;
			pCmdList->GetLatestCommandList()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			// set render targets.
			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
				renderGraph_->GetTarget(gbufferTargetIDs[0])->rtvs[0]->GetDescInfo().cpuHandle,
				renderGraph_->GetTarget(gbufferTargetIDs[1])->rtvs[0]->GetDescInfo().cpuHandle,
				renderGraph_->GetTarget(gbufferTargetIDs[2])->rtvs[0]->GetDescInfo().cpuHandle,
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
			descSet.SetVsCbv(0, hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetPsCbv(0, hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetPsSampler(0, linearSampler_->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoMesh_->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// draw meshes.
			uint meshIndex = 0;
			for (auto&& mesh : sceneMeshes_)
			{
				// set mesh constant.
				descSet.SetVsCbv(1, MeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);

				auto meshRes = mesh->GetParentResource();
				auto&& submeshes = meshRes->GetSubmeshes();
				auto submesh_count = submeshes.size();
				for (int i = 0; i < submesh_count; i++)
				{
					auto&& submesh = submeshes[i];
					auto&& material = meshRes->GetMaterials()[submesh.materialIndex];
					auto bc_tex_res = const_cast<sl12::ResourceItemTexture*>(material.baseColorTex.GetItem<sl12::ResourceItemTexture>());
					auto nm_tex_res = const_cast<sl12::ResourceItemTexture*>(material.normalTex.GetItem<sl12::ResourceItemTexture>());
					auto orm_tex_res = const_cast<sl12::ResourceItemTexture*>(material.ormTex.GetItem<sl12::ResourceItemTexture>());
					auto&& base_color_srv = bc_tex_res->GetTextureView();

					descSet.SetPsSrv(0, bc_tex_res->GetTextureView().GetDescInfo().cpuHandle);
					descSet.SetPsSrv(1, nm_tex_res->GetTextureView().GetDescInfo().cpuHandle);
					descSet.SetPsSrv(2, orm_tex_res->GetTextureView().GetDescInfo().cpuHandle);

					pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);

					const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
						sl12::MeshManager::CreateVertexView(meshRes->GetPositionHandle(), submesh.positionOffsetBytes, submesh.positionSizeBytes, sl12::ResourceItemMesh::GetPositionStride()),
						sl12::MeshManager::CreateVertexView(meshRes->GetNormalHandle(), submesh.normalOffsetBytes, submesh.normalSizeBytes, sl12::ResourceItemMesh::GetNormalStride()),
						sl12::MeshManager::CreateVertexView(meshRes->GetTangentHandle(), submesh.tangentOffsetBytes, submesh.tangentSizeBytes, sl12::ResourceItemMesh::GetTangentStride()),
						sl12::MeshManager::CreateVertexView(meshRes->GetTexcoordHandle(), submesh.texcoordOffsetBytes, submesh.texcoordSizeBytes, sl12::ResourceItemMesh::GetTexcoordStride()),
					};
					pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

					auto ibv = sl12::MeshManager::CreateIndexView(meshRes->GetIndexHandle(), submesh.indexOffsetBytes, submesh.indexSizeBytes, sl12::ResourceItemMesh::GetIndexStride());
					pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

					pCmdList->GetLatestCommandList()->DrawIndexedInstanced(submesh.indexCount, 1, 0, 0, 0);
				}

				meshIndex++;
			}
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
			descSet.SetCsCbv(0, hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(0, renderGraph_->GetTarget(gbufferTargetIDs[0])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(1, renderGraph_->GetTarget(gbufferTargetIDs[1])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(2, renderGraph_->GetTarget(gbufferTargetIDs[2])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(3, renderGraph_->GetTarget(gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsUav(0, renderGraph_->GetTarget(accumTargetID)->uavs[0]->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoLighting_->GetPSO());
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
			GPU_MARKER(pCmdList, 2, "TonemapPass");

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
			descSet.SetPsSrv(0, renderGraph_->GetTarget(accumTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoTonemap_->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);

			// draw fullscreen.
			pCmdList->GetLatestCommandList()->DrawIndexedInstanced(3, 1, 0, 0, 0);
		}
		renderGraph_->EndPass();
	}
	else
	{
		// create resources.
		std::vector<sl12::ResourceItemMesh::Material> materials;
		CreateBuffers(pCmdList, materials);

		// visibility pass.
		pTimestamp->Query(pCmdList);
		renderGraph_->BeginPass(pCmdList, 0);
		{
			// create CBs.
			std::vector<sl12::CbvHandle> MeshCBs;
			std::vector<sl12::CbvHandle> VisCBs;
			{
				sl12::u32 meshIndex = 0;
				sl12::u32 drawCallIndex = 0;
				for (auto&& mesh : sceneMeshes_)
				{
					// set mesh constant.
					MeshCB cbMesh;
					cbMesh.mtxLocalToWorld = mesh->GetMtxLocalToWorld();
					cbMesh.mtxPrevLocalToWorld = mesh->GetMtxPrevLocalToWorld();
					MeshCBs.push_back(cbvMan_->GetTemporal(&cbMesh, sizeof(cbMesh)));

					auto meshRes = mesh->GetParentResource();
					auto&& submeshes = meshRes->GetSubmeshes();
					auto submesh_count = submeshes.size();
					for (int i = 0; i < submesh_count; i++, drawCallIndex++)
					{
						VisibilityCB cbVis;
						cbVis.drawCallIndex = drawCallIndex;
						VisCBs.push_back(cbvMan_->GetTemporal(&cbVis, sizeof(cbVis)));
					}

					meshIndex++;
				}
			}
		
			GPU_MARKER(pCmdList, 0, "VisibilityPass");

			// output barrier.
			renderGraph_->BarrierOutputsAll(pCmdList);

			// clear depth.
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = renderGraph_->GetTarget(gbufferTargetIDs[3])->dsvs[0]->GetDescInfo().cpuHandle;
			pCmdList->GetLatestCommandList()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			// set render targets.
			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
				renderGraph_->GetTarget(visibilityTargetID)->rtvs[0]->GetDescInfo().cpuHandle,
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
			descSet.SetVsCbv(0, hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetPsCbv(0, hSceneCB.GetCBV()->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoVisibility_->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// draw meshes.
			sl12::u32 meshIndex = 0;
			sl12::u32 drawCallIndex = 0;
			for (auto&& mesh : sceneMeshes_)
			{
				// set mesh constant.
				descSet.SetVsCbv(1, MeshCBs[meshIndex].GetCBV()->GetDescInfo().cpuHandle);

				auto meshRes = mesh->GetParentResource();
				auto&& submeshes = meshRes->GetSubmeshes();
				auto submesh_count = submeshes.size();
				for (int i = 0; i < submesh_count; i++, drawCallIndex++)
				{
					auto&& submesh = submeshes[i];

					descSet.SetPsCbv(1, VisCBs[drawCallIndex].GetCBV()->GetDescInfo().cpuHandle);

					pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);
					
					const D3D12_VERTEX_BUFFER_VIEW vbvs[] = {
						sl12::MeshManager::CreateVertexView(meshRes->GetPositionHandle(), submesh.positionOffsetBytes, submesh.positionSizeBytes, sl12::ResourceItemMesh::GetPositionStride()),
					};
					pCmdList->GetLatestCommandList()->IASetVertexBuffers(0, ARRAYSIZE(vbvs), vbvs);

					auto ibv = sl12::MeshManager::CreateIndexView(meshRes->GetIndexHandle(), submesh.indexOffsetBytes, submesh.indexSizeBytes, sl12::ResourceItemMesh::GetIndexStride());
					pCmdList->GetLatestCommandList()->IASetIndexBuffer(&ibv);

					pCmdList->GetLatestCommandList()->DrawIndexedInstanced(submesh.indexCount, 1, 0, 0, 0);
				}

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

			D3D12_CPU_DESCRIPTOR_HANDLE dsv = renderGraph_->GetTarget(matDepthTargetID)->dsvs[0]->GetDescInfo().cpuHandle;
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
			descSet.SetPsSrv(0, renderGraph_->GetTarget(visibilityTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(1, submeshBV_->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(2, drawCallBV_->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(3, renderGraph_->GetTarget(gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetPsUav(0, renderGraph_->GetTarget(accumTargetID)->uavs[0]->GetDescInfo().cpuHandle);

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
			cb.materialMax = (sl12::u32)materials.size();
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
			descSet.SetCsCbv(0, hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetCsCbv(1, hTileCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(0, renderGraph_->GetTarget(visibilityTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(1, submeshBV_->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(2, drawCallBV_->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(3, renderGraph_->GetTarget(gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
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
				renderGraph_->GetTarget(gbufferTargetIDs[0])->rtvs[0]->GetDescInfo().cpuHandle,
				renderGraph_->GetTarget(gbufferTargetIDs[1])->rtvs[0]->GetDescInfo().cpuHandle,
				renderGraph_->GetTarget(gbufferTargetIDs[2])->rtvs[0]->GetDescInfo().cpuHandle,
			};
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = renderGraph_->GetTarget(matDepthTargetID)->dsvs[0]->GetDescInfo().cpuHandle;
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
			descSet.SetVsCbv(0, hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetVsCbv(1, hTileCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetVsSrv(0, tileIndexBV_->GetDescInfo().cpuHandle);
			descSet.SetPsCbv(0, hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(0, renderGraph_->GetTarget(visibilityTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(1, meshMan_->GetVertexBufferSRV()->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(2, meshMan_->GetIndexBufferSRV()->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(3, instanceBV_->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(4, submeshBV_->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(5, drawCallBV_->GetDescInfo().cpuHandle);
			descSet.SetPsSrv(6, renderGraph_->GetTarget(gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetPsSampler(0, linearSampler_->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoMaterialTile_->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			
			sl12::u32 matIndex = 0;
			for (auto&& material : materials)
			{
				MaterialTileCB cb;
				cb.materialIndex = matIndex;
				sl12::CbvHandle hMatTileCB = cbvMan_->GetTemporal(&cb, sizeof(cb));

				auto bc_tex_res = const_cast<sl12::ResourceItemTexture*>(material.baseColorTex.GetItem<sl12::ResourceItemTexture>());
				auto nm_tex_res = const_cast<sl12::ResourceItemTexture*>(material.normalTex.GetItem<sl12::ResourceItemTexture>());
				auto orm_tex_res = const_cast<sl12::ResourceItemTexture*>(material.ormTex.GetItem<sl12::ResourceItemTexture>());

				descSet.SetVsCbv(2, hMatTileCB.GetCBV()->GetDescInfo().cpuHandle);
				descSet.SetPsSrv(7, bc_tex_res->GetTextureView().GetDescInfo().cpuHandle);
				descSet.SetPsSrv(8, nm_tex_res->GetTextureView().GetDescInfo().cpuHandle);
				descSet.SetPsSrv(9, orm_tex_res->GetTextureView().GetDescInfo().cpuHandle);

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
			descSet.SetCsCbv(0, hSceneCB.GetCBV()->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(0, renderGraph_->GetTarget(gbufferTargetIDs[0])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(1, renderGraph_->GetTarget(gbufferTargetIDs[1])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(2, renderGraph_->GetTarget(gbufferTargetIDs[2])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsSrv(3, renderGraph_->GetTarget(gbufferTargetIDs[3])->textureSrvs[0]->GetDescInfo().cpuHandle);
			descSet.SetCsUav(0, renderGraph_->GetTarget(accumTargetID)->uavs[0]->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoLighting_->GetPSO());
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
			descSet.SetPsSrv(0, renderGraph_->GetTarget(accumTargetID)->textureSrvs[0]->GetDescInfo().cpuHandle);

			// set pipeline.
			pCmdList->GetLatestCommandList()->SetPipelineState(psoTonemap_->GetPSO());
			pCmdList->GetLatestCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmdList->SetGraphicsRootSignatureAndDescriptorSet(&rsVsPs_, &descSet);

			// draw fullscreen.
			pCmdList->GetLatestCommandList()->DrawIndexedInstanced(3, 1, 0, 0, 0);
		}
		renderGraph_->EndPass();
	}

	// draw GUI.
	pTimestamp->Query(pCmdList);
	gui_->LoadDrawCommands(pCmdList);
	
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
	device_.Present(0);

	// execute current frame render.
	mainCmdList_->Execute();

	return true;
}

void SampleApplication::CreateBuffers(sl12::CommandList* pCmdList, std::vector<sl12::ResourceItemMesh::Material>& outMaterials)
{
	// count mesh data.
	std::map<const sl12::ResourceItemMesh*, sl12::u32> meshMap;
	std::vector<const sl12::ResourceItemMesh*> meshList;
	std::vector<sl12::u32> submeshOffsets;
	sl12::u32 meshCount = 0;
	sl12::u32 submeshCount = 0;
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
		}
		drawCallCount += (sl12::u32)meshRes->GetSubmeshes().size();
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
		desc.size = sceneMeshes_.size() * sizeof(InstanceData);
		desc.stride = sizeof(InstanceData);
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
		desc.size = submeshCount * sizeof(SubmeshData);
		desc.stride = sizeof(SubmeshData);
		desc.usage = sl12::ResourceUsage::ShaderResource;
		submeshB_->Initialize(&device_, desc);
		submeshBV_->Initialize(&device_, &submeshB_, 0, submeshCount, sizeof(SubmeshData));
	}
	if (!drawCallB_.IsValid())
	{
		drawCallB_ = sl12::MakeUnique<sl12::Buffer>(&device_);
		drawCallBV_ = sl12::MakeUnique<sl12::BufferView>(&device_);

		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Default;
		desc.size = drawCallCount * sizeof(DrawCallData);
		desc.stride = sizeof(DrawCallData);
		desc.usage = sl12::ResourceUsage::ShaderResource;
		drawCallB_->Initialize(&device_, desc);
		drawCallBV_->Initialize(&device_, &drawCallB_, 0, drawCallCount, sizeof(DrawCallData));
	}

	// create copy source.
	UniqueHandle<sl12::Buffer> instanceSrc = sl12::MakeUnique<sl12::Buffer>(&device_);
	UniqueHandle<sl12::Buffer> submeshSrc = sl12::MakeUnique<sl12::Buffer>(&device_);
	UniqueHandle<sl12::Buffer> drawCallSrc = sl12::MakeUnique<sl12::Buffer>(&device_);
	{
		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Dynamic;
		desc.size = sceneMeshes_.size() * sizeof(InstanceData);
		desc.stride = sizeof(InstanceData);
		desc.usage = sl12::ResourceUsage::Unknown;
		instanceSrc->Initialize(&device_, desc);
	}
	{
		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Dynamic;
		desc.size = submeshCount * sizeof(SubmeshData);
		desc.stride = sizeof(SubmeshData);
		desc.usage = sl12::ResourceUsage::Unknown;
		submeshSrc->Initialize(&device_, desc);
	}
	{
		sl12::BufferDesc desc;
		desc.heap = sl12::BufferHeap::Dynamic;
		desc.size = drawCallCount * sizeof(DrawCallData);
		desc.stride = sizeof(DrawCallData);
		desc.usage = sl12::ResourceUsage::Unknown;
		drawCallSrc->Initialize(&device_, desc);
	}
	InstanceData* meshData = (InstanceData*)instanceSrc->Map();
	SubmeshData* submeshData = (SubmeshData*)submeshSrc->Map();
	DrawCallData* drawCallData = (DrawCallData*)drawCallSrc->Map();

	// fill source.
	sl12::u32 materialOffset = 0;
	for (auto meshRes : meshList)
	{
		auto&& submeshes = meshRes->GetSubmeshes();
		for (auto&& submesh : submeshes)
		{
			submeshData->materialIndex = materialOffset + submesh.materialIndex;
			submeshData->posOffset = (sl12::u32)(meshRes->GetPositionHandle().offset + submesh.positionOffsetBytes);
			submeshData->normalOffset = (sl12::u32)(meshRes->GetNormalHandle().offset + submesh.normalOffsetBytes);
			submeshData->tangentOffset = (sl12::u32)(meshRes->GetTangentHandle().offset + submesh.tangentOffsetBytes);
			submeshData->uvOffset = (sl12::u32)(meshRes->GetTexcoordHandle().offset + submesh.texcoordOffsetBytes);
			submeshData->indexOffset = (sl12::u32)(meshRes->GetIndexHandle().offset + submesh.indexOffsetBytes);
			submeshData++;
		}

		for (auto&& mat : meshRes->GetMaterials())
		{
			outMaterials.push_back(mat);
		}
		materialOffset += (sl12::u32)meshRes->GetMaterials().size();
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
			drawCallData->instanceIndex = instanceIndex;
			drawCallData->submeshIndex = submeshOffset + i;
			drawCallData++;
		}

		instanceIndex++;
	}
	instanceSrc->Unmap();
	submeshSrc->Unmap();
	drawCallSrc->Unmap();

	// create draw args.
	UINT tileXCount = (displayWidth_ + CLASSIFY_TILE_WIDTH - 1) / CLASSIFY_TILE_WIDTH;
	UINT tileYCount = (displayHeight_ + CLASSIFY_TILE_WIDTH - 1) / CLASSIFY_TILE_WIDTH;
	UINT tileMax = tileXCount * tileYCount;
	if (drawArgB_.IsValid() && (drawArgB_->GetBufferDesc().size < tileDrawIndirect_->GetStride() * outMaterials.size()))
	{
		drawArgB_.Reset();
		drawArgUAV_.Reset();
	}
	if (tileIndexB_.IsValid() && (tileIndexB_->GetBufferDesc().size < sizeof(sl12::u32) * tileMax * outMaterials.size()))
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
		desc.size = tileDrawIndirect_->GetStride() * outMaterials.size();
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
		desc.size = sizeof(sl12::u32) * tileMax * outMaterials.size();
		desc.stride = 0;
		desc.usage = sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess;
		tileIndexB_->Initialize(&device_, desc);
		tileIndexBV_->Initialize(&device_, &tileIndexB_, 0, 0, 0);
		tileIndexUAV_->Initialize(&device_, &tileIndexB_, 0, 0, 0, 0);
	}

	// copy commands.
	pCmdList->TransitionBarrier(&instanceB_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
	pCmdList->TransitionBarrier(&submeshB_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
	pCmdList->TransitionBarrier(&drawCallB_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
	pCmdList->GetLatestCommandList()->CopyBufferRegion(instanceB_->GetResourceDep(), 0, instanceSrc->GetResourceDep(), 0, instanceSrc->GetBufferDesc().size);
	pCmdList->GetLatestCommandList()->CopyBufferRegion(submeshB_->GetResourceDep(), 0, submeshSrc->GetResourceDep(), 0, submeshSrc->GetBufferDesc().size);
	pCmdList->GetLatestCommandList()->CopyBufferRegion(drawCallB_->GetResourceDep(), 0, drawCallSrc->GetResourceDep(), 0, drawCallSrc->GetBufferDesc().size);
	pCmdList->TransitionBarrier(&instanceB_, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	pCmdList->TransitionBarrier(&submeshB_, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	pCmdList->TransitionBarrier(&drawCallB_, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
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


//	EOF
