#include "scene.h"
#include "shader_types.h"
#include "sl12/resource_texture.h"
#include "sl12/descriptor_set.h"
#include "sl12/command_list.h"
#include "sl12/string_util.h"
#include "pass/gbuffer_pass.h"
#include "pass/shadowmap_pass.h"
#include "pass/utility_pass.h"
#include "pass/indirect_light_pass.h"
#include "pass/visibility_pass.h"

#define NOMINMAX
#include <windowsx.h>
#include <memory>
#include <random>

#define USE_IN_CPP
#include "../shaders/cbuffer.hlsli"
#include "pass/render_resource_settings.h"

namespace
{
	struct MeshletBound
	{
		DirectX::XMFLOAT3		aabbMin;
		DirectX::XMFLOAT3		aabbMax;
		DirectX::XMFLOAT3		coneApex;
		DirectX::XMFLOAT3		coneAxis;
		float					coneCutoff;
		sl12::u32				pad[3];
	};	// struct MeshletBound
}

//----------------
//----
RenderSystem::RenderSystem(sl12::Device* pDev, const std::string& resDir, const ShaderInitDesc& shaderDesc)
{
	// init mesh manager.
	const size_t kVertexBufferSize = 512 * 1024 * 1024;		// 512MB
	const size_t kIndexBufferSize = 64 * 1024 * 1024;		// 64MB
	meshMan_ = sl12::MakeUnique<sl12::MeshManager>(nullptr, pDev, kVertexBufferSize, kIndexBufferSize);

	// init texture streamer.
	texStreamer_ = sl12::MakeUnique<sl12::TextureStreamer>(nullptr);
	bool isInitTexStreamer = texStreamer_->Initialize(pDev);
	assert(isInitTexStreamer);

	// init resource loader.
	resLoader_ = sl12::MakeUnique<sl12::ResourceLoader>(nullptr);
	bool isInitResLoader = resLoader_->Initialize(pDev, &meshMan_, resDir);
	assert(isInitResLoader);

	// init shader manager.
	shaderMan_ = sl12::MakeUnique<sl12::ShaderManager>(nullptr);
	bool isInitShaderMan = shaderMan_->Initialize(pDev, &shaderDesc.includeDirs, shaderDesc.pdbType, &shaderDesc.pdbDir);
	assert(isInitShaderMan);

	// init cbv manager.
	cbvMan_ = sl12::MakeUnique<sl12::CbvManager>(nullptr, pDev);

	// init samplers.
	{
		linearWrapSampler_ = sl12::MakeUnique<sl12::Sampler>(pDev);
		linearClampSampler_ = sl12::MakeUnique<sl12::Sampler>(pDev);
		shadowSampler_ = sl12::MakeUnique<sl12::Sampler>(pDev);
		envSampler_ = sl12::MakeUnique<sl12::Sampler>(pDev);

		D3D12_SAMPLER_DESC desc{};
		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.MaxLOD = FLT_MAX;
		desc.MinLOD = 0.0f;
		desc.MipLODBias = 0.0f;
		linearWrapSampler_->Initialize(pDev, desc);

		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		linearClampSampler_->Initialize(pDev, desc);

		desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER;
		shadowSampler_->Initialize(pDev, desc);

		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.MaxLOD = FLT_MAX;
		desc.MinLOD = 0.0f;
		desc.MipLODBias = 0.0f;
		desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
		envSampler_->Initialize(pDev, desc);
	}
	
	// compile shaders.
	std::vector<std::string> Args;
	Args.push_back("-O3");
	for (int i = 0; i < ShaderName::MAX; i++)
	{
		const char* file = kShaderFileAndEntry[i * 2 + 0];
		const char* entry = kShaderFileAndEntry[i * 2 + 1];
		auto handle = shaderMan_->CompileFromFile(
			sl12::JoinPath(shaderDesc.baseDir, file),
			entry, sl12::GetShaderTypeFromFileName(file), 6, 8, &Args, nullptr);
		hShaders_.push_back(handle);
	}
}

//----
RenderSystem::~RenderSystem()
{
	linearWrapSampler_.Reset();
	linearClampSampler_.Reset();
	shadowSampler_.Reset();
	envSampler_.Reset();

	cbvMan_.Reset();
	shaderMan_.Reset();
	resLoader_.Reset();
	texStreamer_.Reset();
	meshMan_.Reset();
}

//----
void RenderSystem::WaitLoadAndCompile()
{
	while (shaderMan_->IsCompiling() || resLoader_->IsLoading())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}


//----------------
//----
Scene::Scene()
{}

//----
Scene::~Scene()
{
	Finalize();
}

//----
bool Scene::Initialize(sl12::Device* pDev, RenderSystem* pRenderSys, int meshType)
{
	pDevice_ = pDev;
	pRenderSystem_ = pRenderSys;

	// resource load request.
	auto resLoader = pRenderSys->GetResourceLoader();
	if (meshType == 0)
	{
		hSuzanneMesh_ = resLoader->LoadRequest<sl12::ResourceItemMesh>("mesh/hp_suzanne/hp_suzanne.rmesh");
	}
	else if (meshType == 1)
	{
		hSponzaMesh_ = resLoader->LoadRequest<sl12::ResourceItemMesh>("mesh/sponza/sponza.rmesh");
		hSphereMesh_ = resLoader->LoadRequest<sl12::ResourceItemMesh>("mesh/sphere/sphere.rmesh");
	}
	else if (meshType == 2)
	{
		hSponzaMesh_ = resLoader->LoadRequest<sl12::ResourceItemMesh>("mesh/IntelSponza/IntelSponza.rmesh");
		hCurtainMesh_ = resLoader->LoadRequest<sl12::ResourceItemMesh>("mesh/IntelCurtain/IntelCurtain.rmesh");
	}
	else if (meshType == 3)
	{
		hBistroMesh_ = resLoader->LoadRequest<sl12::ResourceItemMesh>("mesh/Bistro/BistroExterior.rmesh");
	}
	hDetailTex_ = resLoader->LoadRequest<sl12::ResourceItemTexture>("texture/detail_normal.dds");
	hDotTex_ = resLoader->LoadRequest<sl12::ResourceItemTexture>("texture/dot_normal.dds");
	hHDRI_ = resLoader->LoadRequest<sl12::ResourceItemTexture>("texture/citrus_orchard_road_puresky_4k.exr");

	meshletResource_ = sl12::MakeUnique<MeshletResource>(nullptr);
	
	return true;
}

//----
void Scene::Finalize()
{
	sceneMeshes_.clear();

	miplevelBuffer_.Reset();
	miplevelCopySrc_.Reset();
	miplevelUAV_.Reset();
	miplevelReadbacks_[0].Reset();
	miplevelReadbacks_[1].Reset();
}

//----
void Scene::SetViewportResolution(sl12::u32 width, sl12::u32 height)
{
	screenWidth_ = width;
	screenHeight_ = height;
}

//----
bool Scene::CreateSceneMeshes(int meshType)
{
	if (meshType == 0)
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
				auto mesh = std::make_shared<sl12::SceneMesh>(pDevice_, hSuzanneMesh_.GetItem<sl12::ResourceItemMesh>());
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
	else if (meshType == 1)
	{
		// sponza
		{
			auto mesh = std::make_shared<sl12::SceneMesh>(pDevice_, hSponzaMesh_.GetItem<sl12::ResourceItemMesh>());
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
			auto mesh = std::make_shared<sl12::SceneMesh>(pDevice_, hSphereMesh_.GetItem<sl12::ResourceItemMesh>());
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
	else if (meshType == 2)
	{
		// sponza
		{
			auto mesh = std::make_shared<sl12::SceneMesh>(pDevice_, hSponzaMesh_.GetItem<sl12::ResourceItemMesh>());
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
			auto mesh = std::make_shared<sl12::SceneMesh>(pDevice_, hCurtainMesh_.GetItem<sl12::ResourceItemMesh>());
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
	else if (meshType == 3)
	{
		// Bistro
		{
			auto mesh = std::make_shared<sl12::SceneMesh>(pDevice_, hBistroMesh_.GetItem<sl12::ResourceItemMesh>());
			DirectX::XMFLOAT3 pos(1240.0f, 920.0f, -20.0f);
			DirectX::XMFLOAT3 scl(20.0f, 20.0f, 20.0f);
			DirectX::XMFLOAT4X4 mat;
			DirectX::XMMATRIX m = DirectX::XMMatrixScaling(scl.x, scl.y, scl.z)
									* DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
			DirectX::XMStoreFloat4x4(&mat, m);
			mesh->SetMtxLocalToWorld(mat);

			sceneMeshes_.push_back(mesh);
		}
	}

	ComputeSceneAABB();
	CreateMeshletResource();
	CreateMiplevelFeedback();

	return true;
}

//----
void Scene::ComputeSceneAABB()
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

//----
void Scene::CreateMiplevelFeedback()
{
	auto&& materials = meshletResource_->GetWorldMaterials();

	// create miplevel feedback buffer.
	{
		sl12::BufferDesc desc{};
		desc.heap = sl12::BufferHeap::Default;
		desc.size = sizeof(sl12::u32) * materials.size();
		desc.stride = 0;
		desc.usage = sl12::ResourceUsage::UnorderedAccess;
		desc.initialState = D3D12_RESOURCE_STATE_COMMON;

		miplevelBuffer_ = sl12::MakeUnique<sl12::Buffer>(pDevice_);
		miplevelBuffer_->Initialize(pDevice_, desc);
		miplevelUAV_ = sl12::MakeUnique<sl12::UnorderedAccessView>(pDevice_);
		miplevelUAV_->Initialize(pDevice_, &miplevelBuffer_, 0, 0, 0, 0);

		desc.heap = sl12::BufferHeap::Dynamic;
		desc.usage = sl12::ResourceUsage::Unknown;
		desc.initialState = D3D12_RESOURCE_STATE_COMMON;
		miplevelCopySrc_ = sl12::MakeUnique<sl12::Buffer>(pDevice_);
		miplevelCopySrc_->Initialize(pDevice_, desc);

		void* p = miplevelCopySrc_->Map();
		memset(p, 0xff, desc.size);
		miplevelCopySrc_->Unmap();
	}

	neededMiplevels_.resize(materials.size());
	for (auto&& s : neededMiplevels_)
	{
		s.minLevel = 0xff;
		s.latestLevel = 0xff;
		s.time = 0;
	}
	miplevelReadbacks_[0].Reset();
	miplevelReadbacks_[1].Reset();
}

//----
void Scene::CreateMeshletBounds(sl12::CommandList* pCmdList)
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
		B = sl12::MakeUnique<sl12::Buffer>(pDevice_);
		BV = sl12::MakeUnique<sl12::BufferView>(pDevice_);
		UniqueHandle<sl12::Buffer> UploadB = sl12::MakeUnique<sl12::Buffer>(pDevice_);
		
		sl12::BufferDesc desc{};
		desc.heap = sl12::BufferHeap::Default;
		desc.stride = sizeof(MeshletBound);
		desc.size = desc.stride * meshletTotal;
		desc.usage = sl12::ResourceUsage::ShaderResource;
		desc.initialState = D3D12_RESOURCE_STATE_COMMON;

		B->Initialize(pDevice_, desc);
		BV->Initialize(pDevice_, &B, 0, 0, (sl12::u32)desc.stride);

		desc.heap = sl12::BufferHeap::Dynamic;
		UploadB->Initialize(pDevice_, desc);

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

	for (auto&& mesh : sceneMeshes_)
	{
		sl12::ResourceHandle Handle = mesh->GetParentResource()->GetHandle();
		sl12::u64 ID = Handle.GetID();
		if (meshletBoundsBuffers_.find(ID) == meshletBoundsBuffers_.end())
		{
			UniqueHandle<sl12::Buffer> Buffer;
			UniqueHandle<sl12::BufferView> View;
			CreateAndCopy(Handle, Buffer, View);
			meshletBoundsBuffers_[ID] = std::move(Buffer);
			meshletBoundsSRVs_[ID] = std::move(View);
		}
	}
}

//----
bool Scene::InitRenderPass()
{
	renderGraph_ = sl12::MakeUnique<sl12::RenderGraph>(nullptr);
	renderGraph_->Initialize(pDevice_);

	{
		auto pass = std::make_unique<MeshletArgCopyPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::MeshletArgCopy] = renderGraph_->AddPass(kMeshletArgCopyPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<MeshletCullingPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::MeshletCulling] = renderGraph_->AddPass(kMeshletCullingPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<ClearMiplevelPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::ClearMiplevel] = renderGraph_->AddPass(kClearMiplevelPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<FeedbackMiplevelPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::FeedbackMiplevel] = renderGraph_->AddPass(kFeedbackMiplevelPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<DepthPrePass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::DepthPre] = renderGraph_->AddPass(kDepthPrePass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<GBufferPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::GBuffer] = renderGraph_->AddPass(kGBufferPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<ShadowMapPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::ShadowMap] = renderGraph_->AddPass(kShadowMapPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<ShadowExpPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::ShadowExp] = renderGraph_->AddPass(kShadowExpPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<ShadowExpBlurPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::ShadowBlurX] = renderGraph_->AddPass(kShadowBlurXPass, pass.get());
		passNodes_[AppPassType::ShadowBlurY] = renderGraph_->AddPass(kShadowBlurYPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<LightingPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::Lighting] = renderGraph_->AddPass(kLightingPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<HiZPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::HiZ] = renderGraph_->AddPass(kHiZPass, pass.get());
		passNodes_[AppPassType::HiZafterFirstCull] = renderGraph_->AddPass(kHiZafterFirstCullPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<TonemapPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::Tonemap] = renderGraph_->AddPass(kTonemapPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<DeinterleavePass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::Deinterleave] = renderGraph_->AddPass(kDeinterleavePass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<ScreenSpaceAOPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::SSAO] = renderGraph_->AddPass(kScreenSpaceAOPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<DenoisePass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::Denoise] = renderGraph_->AddPass(kDenoisePass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<IndirectLightPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::IndirectLight] = renderGraph_->AddPass(kIndirectLightPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<BufferReadyPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::BufferReady] = renderGraph_->AddPass(kBufferReadyPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<VisibilityVsPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::VisibilityVs] = renderGraph_->AddPass(kVisibilityVsPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<VisibilityMsPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::VisibilityMs1st] = renderGraph_->AddPass(kVisibilityMs1stPass, pass.get());
		passNodes_[AppPassType::VisibilityMs2nd] = renderGraph_->AddPass(kVisibilityMs2ndPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<MaterialDepthPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::MaterialDepth] = renderGraph_->AddPass(kMaterialDepthPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<ClassifyPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::Classify] = renderGraph_->AddPass(kClassifyPass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<MaterialTilePass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::MaterialTile] = renderGraph_->AddPass(kMaterialTilePass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<MaterialResolvePass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::MaterialResolve] = renderGraph_->AddPass(kMaterialResolvePass, pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<MaterialComputeBinningPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::MaterialComputeBinning] = renderGraph_->AddPass(sl12::RenderPassID("MaterialComputeBinningPass"), pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<MaterialComputeGBufferPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::MaterialComputeGBuffer] = renderGraph_->AddPass(sl12::RenderPassID("MaterialComputeGBufferPass"), pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<MaterialTileBinningPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::MaterialTileBinning] = renderGraph_->AddPass(sl12::RenderPassID("MaterialTileBinningPass"), pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<MaterialTileGBufferPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::MaterialTileGBuffer] = renderGraph_->AddPass(sl12::RenderPassID("MaterialTileGBufferPass"), pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<GenerateVrsPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::GenerateVRS] = renderGraph_->AddPass(sl12::RenderPassID("GenerateVrsPass"), pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<ReprojectVrsPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::ReprojectVRS] = renderGraph_->AddPass(sl12::RenderPassID("ReprojectVrsPass"), pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<PrefixSumTestPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::PrefixSumTest] = renderGraph_->AddPass(sl12::RenderPassID("PrefixSumTest"), pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<XluPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::Xlu] = renderGraph_->AddPass(sl12::RenderPassID("Xlu"), pass.get());
		passes_.push_back(std::move(pass));
	}
	{
		auto pass = std::make_unique<DebugPass>(pDevice_, pRenderSystem_, this);
		passNodes_[AppPassType::Debug] = renderGraph_->AddPass(sl12::RenderPassID("Debug"), pass.get());
		passes_.push_back(std::move(pass));
	}

	RenderPassSetupDesc defaultDesc;
	SetupRenderPassGraph(defaultDesc);
	
	return true;
}

//----
void Scene::SetupRenderPassGraph(const RenderPassSetupDesc& desc)
{
	bool bNeedDeinterleave = desc.ssaoType == 2 && desc.bNeedDeinterleave;

	// setting.
	for (auto&& pass : passes_)
	{
		pass->SetPassSettings(desc);
	}

	bool bEnableMeshletCulling = !desc.bUseVisibilityBuffer || !desc.bUseMeshShader;
	bool bDirectGBufferRender = !desc.bUseVisibilityBuffer;
	bool bEnableVRS = desc.bUseVRS;

	sl12::RenderGraph::Node node;
	
	renderGraph_->ClearAllGraphEdges();
	// graphics queue.
	// node = node.AddChild(passNodes_[AppPassType::PrefixSumTest]); // TEST: Prefux Sum Test Pass.
	if (bEnableMeshletCulling)
	{
		node = node.AddChild(passNodes_[AppPassType::MeshletArgCopy]);
	}
	node = node.AddChild(passNodes_[AppPassType::ClearMiplevel]);
	if (bDirectGBufferRender)
	{
	 	// direct gbuffer redering.
		node = node.AddChild(passNodes_[AppPassType::DepthPre]).AddChild(passNodes_[AppPassType::GBuffer]);
	}
	else
	{
	 	// visibility rendering.
	 	if (!desc.bUseMeshShader)
	 	{
	 		node = node.AddChild(passNodes_[AppPassType::VisibilityVs]);
	 	}
	    else
	    {
	    	node = node.AddChild(passNodes_[AppPassType::VisibilityMs1st])
	    		.AddChild(passNodes_[AppPassType::HiZafterFirstCull])
	    		.AddChild(passNodes_[AppPassType::VisibilityMs2nd]);
	    }
		// VRS reprojection.
		if (bEnableVRS)
		{
			node = node.AddChild(passNodes_[AppPassType::ReprojectVRS]);
		}
		// visibility to gbuffer.
		if (desc.visToGBufferType == 0)
		{
			// Depth & Tile
			node = node.AddChild(passNodes_[AppPassType::MaterialDepth])
				.AddChild(passNodes_[AppPassType::Classify])
				.AddChild(passNodes_[AppPassType::MaterialTile]);
		}
		else if (desc.visToGBufferType == 1)
		{
			// Compute Pixel
			node = node.AddChild(passNodes_[AppPassType::MaterialComputeBinning])
				.AddChild(passNodes_[AppPassType::MaterialComputeGBuffer]);
		}
		else if (desc.visToGBufferType == 2)
		{
			// Compute Tile
			node = node.AddChild(passNodes_[AppPassType::MaterialTileBinning])
				.AddChild(passNodes_[AppPassType::MaterialTileGBuffer]);
		}
		else
		{
			// Work Graph
			node = node.AddChild(passNodes_[AppPassType::MaterialResolve]);
		}
	}
	node = node.AddChild(passNodes_[AppPassType::FeedbackMiplevel])
		.AddChild(passNodes_[AppPassType::ShadowMap])
		.AddChild(passNodes_[AppPassType::Lighting])
		.AddChild(passNodes_[AppPassType::HiZ])
		.AddChild(passNodes_[AppPassType::IndirectLight])
		.AddChild(passNodes_[AppPassType::Xlu]);
	if (bEnableVRS)
	{
		node = node.AddChild(passNodes_[AppPassType::GenerateVRS]);
	}
	node = node.AddChild(passNodes_[AppPassType::Tonemap]);
	if (desc.debugMode != 0)
	{
		node = node.AddChild(passNodes_[AppPassType::Debug]);
	}

	// compute queue.
	if (bEnableMeshletCulling)
	{
		node = sl12::RenderGraph::Node()
			.AddChild(passNodes_[AppPassType::MeshletArgCopy])
			.AddChild(passNodes_[AppPassType::MeshletCulling]);
		if (bDirectGBufferRender)
		{
			node = node.AddChild(passNodes_[AppPassType::DepthPre]);
		}
		else
		{
			node = node.AddChild(passNodes_[AppPassType::VisibilityVs]);
		}
	}
	{
		// ssao.
		node = sl12::RenderGraph::Node()
			.AddChild(passNodes_[AppPassType::FeedbackMiplevel]);
		if (bNeedDeinterleave)
		{
			node.AddChild(passNodes_[AppPassType::Deinterleave]);
		}
		node = node.AddChild(passNodes_[AppPassType::SSAO])
			.AddChild(passNodes_[AppPassType::Denoise])
			.AddChild(passNodes_[AppPassType::IndirectLight]);
	}

	// copy queue.
	if (!bDirectGBufferRender)
	{
	 	if (!desc.bUseMeshShader)
	 	{
	 		passNodes_[AppPassType::BufferReady].AddChild(passNodes_[AppPassType::VisibilityVs]);
	 	}
	    else
	    {
	    	passNodes_[AppPassType::BufferReady].AddChild(passNodes_[AppPassType::VisibilityMs1st]);
	    }
	}

	lastRenderPassDesc_ = desc;
}

//----
void Scene::SetupRenderPass(sl12::Texture* pSwapchainTarget, const RenderPassSetupDesc& desc)
{
	if (lastRenderPassDesc_ != desc)
	{
		SetupRenderPassGraph(desc);
	}

	renderGraph_->AddExternalTexture(kSwapchainID, pSwapchainTarget, sl12::TransientState::Present);
	renderGraph_->Compile();
}

void Scene::LoadRenderGraphCommand()
{
	renderGraph_->LoadCommand();
}

void Scene::ExecuteRenderGraphCommand()
{
	renderGraph_->Execute();
}

void Scene::CreateIrradianceMap(sl12::CommandList* pCmdList)
{
	const sl12::u32 kIrradianceWidth = 1024;

	// 初回のみ
	if (irradianceMap_.IsValid())
	{
		return;
	}

	auto hdriTex = hHDRI_.GetItem<sl12::ResourceItemTexture>();
	sl12::u32 width = hdriTex->GetTexture().GetTextureDesc().width;
	sl12::u32 height = hdriTex->GetTexture().GetTextureDesc().height;

	float scale = (float)width / (float)kIrradianceWidth;
	width = kIrradianceWidth;
	height = (sl12::u32)((float)height / scale);
	float mipLevel = std::log2f(scale);

	sl12::TextureDesc desc;
	desc.Initialize2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, 1, sl12::ResourceUsage::ShaderResource | sl12::ResourceUsage::UnorderedAccess);
	irradianceMap_ = sl12::MakeUnique<sl12::Texture>(pDevice_);
	irradianceMap_->Initialize(pDevice_, desc);

	irradianceMapSRV_ = sl12::MakeUnique<sl12::TextureView>(pDevice_);
	irradianceMapSRV_->Initialize(pDevice_, &irradianceMap_);

	UniqueHandle<sl12::UnorderedAccessView> UAV = sl12::MakeUnique<sl12::UnorderedAccessView>(pDevice_);
	UAV->Initialize(pDevice_, &irradianceMap_);

	UniqueHandle<sl12::RootSignature> RootSig = sl12::MakeUnique<sl12::RootSignature>(pDevice_);
	RootSig->Initialize(pDevice_, pRenderSystem_->GetShader(MakeIrradianceC));

	UniqueHandle<sl12::ComputePipelineState> PSO = sl12::MakeUnique<sl12::ComputePipelineState>(pDevice_);
	{
		sl12::ComputePipelineStateDesc psoDesc;
		psoDesc.pRootSignature = &RootSig;
		psoDesc.pCS = pRenderSystem_->GetShader(MakeIrradianceC);
		PSO->Initialize(pDevice_, psoDesc);
	}

	struct CB
	{
		sl12::u32 Width, Height;
		sl12::u32 IterCount;
		float MipLevel;
	};
	CB cbData = { width, height, 5000, mipLevel };
	auto hCBV = pRenderSystem_->GetCbvManager()->GetTemporal(&cbData, sizeof(cbData));

	// set descriptors.
	sl12::DescriptorSet descSet;
	descSet.Reset();
	descSet.SetCsCbv(0, hCBV.GetCBV()->GetDescInfo().cpuHandle);
	descSet.SetCsSrv(0, hdriTex->GetTextureView().GetDescInfo().cpuHandle);
	descSet.SetCsUav(0, UAV->GetDescInfo().cpuHandle);
	descSet.SetCsSampler(0, pRenderSystem_->GetEnvSampler()->GetDescInfo().cpuHandle);

	// set pipeline.
	pCmdList->GetLatestCommandList()->SetPipelineState(PSO->GetPSO());
	pCmdList->SetComputeRootSignatureAndDescriptorSet(&RootSig, &descSet);

	// dispatch.
	UINT x = (width + 7) / 8;
	UINT y = (height + 7) / 8;
	pCmdList->GetLatestCommandList()->Dispatch(x, y, 1);

	pCmdList->AddTransitionBarrier(&irradianceMap_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}

void Scene::CreateMeshletResource()
{
	meshletResource_->CreateResources(pDevice_, sceneMeshes_);
}


//	EOF
