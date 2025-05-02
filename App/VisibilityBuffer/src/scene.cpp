#include "scene.h"
#include "shader_types.h"
#include "sl12/resource_texture.h"
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
	}
	
	// compile shaders.
	for (int i = 0; i < ShaderName::MAX; i++)
	{
		const char* file = kShaderFileAndEntry[i * 2 + 0];
		const char* entry = kShaderFileAndEntry[i * 2 + 1];
		auto handle = shaderMan_->CompileFromFile(
			sl12::JoinPath(shaderDesc.baseDir, file),
			entry, sl12::GetShaderTypeFromFileName(file), 6, 8, nullptr, nullptr);
		hShaders_.push_back(handle);
	}
}

//----
RenderSystem::~RenderSystem()
{
	linearWrapSampler_.Reset();
	linearClampSampler_.Reset();
	shadowSampler_.Reset();

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
	hDetailTex_ = resLoader->LoadRequest<sl12::ResourceItemTexture>("texture/detail_normal.dds");
	hDotTex_ = resLoader->LoadRequest<sl12::ResourceItemTexture>("texture/dot_normal.dds");

	return true;
}

//----
void Scene::Finalize()
{
	sceneMeshes_.clear();

	materialDataB_.Reset();
	materialDataCopyB_.Reset();
	materialDataBV_.Reset();

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

	ComputeSceneAABB();
	CreateMaterialList();

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
void Scene::CreateMaterialList()
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
				work.texHandles.push_back(work.pResMaterial->baseColorTex);
				work.texHandles.push_back(work.pResMaterial->normalTex);
				work.texHandles.push_back(work.pResMaterial->ormTex);

				// add to list.
				workMaterials_.push_back(work);
			}
		}
	}

	// work graph resources.
	{
		sl12::BufferDesc desc{};
		desc.heap = sl12::BufferHeap::Default;
		desc.usage = sl12::ResourceUsage::ShaderResource;
		desc.stride = sizeof(MaterialData);
		desc.size = desc.stride * workMaterials_.size();
		desc.initialState = D3D12_RESOURCE_STATE_COMMON;

		materialDataB_ = sl12::MakeUnique<sl12::Buffer>(pDevice_);
		materialDataCopyB_ = sl12::MakeUnique<sl12::Buffer>(pDevice_);
		materialDataBV_ = sl12::MakeUnique<sl12::BufferView>(pDevice_);
		materialDataB_->Initialize(pDevice_, desc);
		materialDataBV_->Initialize(pDevice_, &materialDataB_, 0, (sl12::u32)workMaterials_.size(), sizeof(MaterialData));
		desc.heap = sl12::BufferHeap::Dynamic;
		materialDataCopyB_->Initialize(pDevice_, desc);

		MaterialData* data = static_cast<MaterialData*>(materialDataCopyB_->Map());
		auto dot_res = const_cast<sl12::ResourceItemTextureBase*>(hDotTex_.GetItem<sl12::ResourceItemTextureBase>());
		bindlessTextures_.clear();
		bindlessTextures_.push_back(pDevice_->GetDummyTextureView(sl12::DummyTex::Black)->GetDescInfo().cpuHandle);
		bindlessTextures_.push_back(pDevice_->GetDummyTextureView(sl12::DummyTex::FlatNormal)->GetDescInfo().cpuHandle);
		bindlessTextures_.push_back(dot_res->GetTextureView().GetDescInfo().cpuHandle);
		for (auto&& work : workMaterials_)
		{
			if (work.psoType == 0)
			{
				data->shaderIndex = 0;
				// default.
				data->colorTexIndex = data->ormTexIndex = 0;
				data->normalTexIndex = 1;
				// each textures.
				auto resTex = work.pResMaterial->baseColorTex.GetItem<sl12::ResourceItemTextureBase>();
				if (resTex)
				{
					data->colorTexIndex = (UINT)bindlessTextures_.size();
					bindlessTextures_.push_back(const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView().GetDescInfo().cpuHandle);
				}
				resTex = work.pResMaterial->normalTex.GetItem<sl12::ResourceItemTextureBase>();
				if (resTex)
				{
					data->normalTexIndex = (UINT)bindlessTextures_.size();
					bindlessTextures_.push_back(const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView().GetDescInfo().cpuHandle);
				}
				resTex = work.pResMaterial->ormTex.GetItem<sl12::ResourceItemTextureBase>();
				if (resTex)
				{
					data->ormTexIndex = (UINT)bindlessTextures_.size();
					bindlessTextures_.push_back(const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView().GetDescInfo().cpuHandle);
				}
			}
			else
			{
				data->shaderIndex = 1;
				data->colorTexIndex = data->ormTexIndex = 0;
				data->normalTexIndex = 2;
			}
			data++;
		}
		materialDataCopyB_->Unmap();
	}

	// create miplevel feedback buffer.
	{
		sl12::BufferDesc desc{};
		desc.heap = sl12::BufferHeap::Default;
		desc.size = sizeof(sl12::u32) * workMaterials_.size();
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

	neededMiplevels_.resize(workMaterials_.size());
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
int Scene::GetMaterialIndex(const sl12::ResourceItemMesh::Material* mat)
{
	auto it = std::find_if(
		workMaterials_.begin(), workMaterials_.end(),
		[mat](const WorkMaterial& rhs){ return mat == rhs.pResMaterial; });
	if (it == workMaterials_.end())
		return -1;
	auto index = std::distance(workMaterials_.begin(), it);
	return (int)index;
};

//----
void Scene::CopyMaterialData(sl12::CommandList* pCmdList)
{
	if (materialDataCopyB_.IsValid())
	{
		pCmdList->AddTransitionBarrier(&materialDataB_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
		pCmdList->AddTransitionBarrier(&materialDataCopyB_, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_SOURCE);
		pCmdList->FlushBarriers();

		pCmdList->GetLatestCommandList()->CopyBufferRegion(materialDataB_->GetResourceDep(), 0, materialDataCopyB_->GetResourceDep(), 0, materialDataCopyB_->GetBufferDesc().size);

		pCmdList->AddTransitionBarrier(&materialDataB_, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
		pCmdList->FlushBarriers();

		materialDataCopyB_.Reset();
	}
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

//----
bool Scene::InitRenderPass()
{
	renderGraph_ = sl12::MakeUnique<sl12::RenderGraph>(nullptr);
	renderGraph_->Initialize(pDevice_);
	
	passes_.emplace(AppPassType::MeshletArgCopy, std::make_unique<MeshletArgCopyPass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::MeshletCulling, std::make_unique<MeshletCullingPass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::ClearMiplevel, std::make_unique<ClearMiplevelPass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::FeedbackMiplevel, std::make_unique<FeedbackMiplevelPass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::DepthPre, std::make_unique<DepthPrePass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::GBuffer, std::make_unique<GBufferPass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::ShadowMap, std::make_unique<ShadowMapPass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::ShadowExp, std::make_unique<ShadowExpPass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::ShadowBlurX, std::make_unique<ShadowExpBlurPass>(pDevice_, pRenderSystem_, this, true));
	passes_.emplace(AppPassType::ShadowBlurY, std::make_unique<ShadowExpBlurPass>(pDevice_, pRenderSystem_, this, false));
	passes_.emplace(AppPassType::Lighting, std::make_unique<LightingPass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::HiZ, std::make_unique<HiZPass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::Tonemap, std::make_unique<TonemapPass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::Deinterleave, std::make_unique<DeinterleavePass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::SSAO, std::make_unique<ScreenSpaceAOPass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::Denoise, std::make_unique<DenoisePass>(pDevice_, pRenderSystem_, this));
	passes_.emplace(AppPassType::IndirectLight, std::make_unique<IndirectLightPass>(pDevice_, pRenderSystem_, this));

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
		pass.second->SetPassSettings(desc);
	}
	
	renderGraph_->ClearPasses();
	renderGraph_->AddPass(passes_[AppPassType::ShadowMap].get(), nullptr);
	renderGraph_->AddPass(passes_[AppPassType::MeshletArgCopy].get(), passes_[AppPassType::ShadowMap].get());
	renderGraph_->AddPass(passes_[AppPassType::MeshletCulling].get(), passes_[AppPassType::MeshletArgCopy].get());
	renderGraph_->AddPass(passes_[AppPassType::ClearMiplevel].get(), passes_[AppPassType::MeshletCulling].get());
	renderGraph_->AddPass(passes_[AppPassType::DepthPre].get(), passes_[AppPassType::ClearMiplevel].get());
	renderGraph_->AddPass(passes_[AppPassType::GBuffer].get(), passes_[AppPassType::DepthPre].get());
	renderGraph_->AddPass(passes_[AppPassType::FeedbackMiplevel].get(), passes_[AppPassType::GBuffer].get());
	renderGraph_->AddPass(passes_[AppPassType::Lighting].get(), passes_[AppPassType::FeedbackMiplevel].get());
	renderGraph_->AddPass(passes_[AppPassType::HiZ].get(), passes_[AppPassType::Lighting].get());
	if (bNeedDeinterleave)
	{
		renderGraph_->AddPass(passes_[AppPassType::Deinterleave].get(), passes_[AppPassType::HiZ].get());
		renderGraph_->AddPass(passes_[AppPassType::SSAO].get(), passes_[AppPassType::Deinterleave].get());
	}
	else
	{
		renderGraph_->AddPass(passes_[AppPassType::SSAO].get(), passes_[AppPassType::HiZ].get());
	}
	renderGraph_->AddPass(passes_[AppPassType::Denoise].get(), passes_[AppPassType::SSAO].get());
	renderGraph_->AddPass(passes_[AppPassType::IndirectLight].get(), passes_[AppPassType::Denoise].get());
	renderGraph_->AddPass(passes_[AppPassType::Tonemap].get(), passes_[AppPassType::IndirectLight].get());

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


//	EOF
