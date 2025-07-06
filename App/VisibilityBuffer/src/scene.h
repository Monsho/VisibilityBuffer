#pragma once

#include <memory>
#include <queue>
#include <vector>

#include "app_pass_base.h"

#include "sl12/resource_loader.h"
#include "sl12/shader_manager.h"
#include "sl12/command_list.h"
#include "sl12/gui.h"
#include "sl12/root_signature.h"
#include "sl12/pipeline_state.h"
#include "sl12/unique_handle.h"
#include "sl12/cbv_manager.h"
#include "sl12/render_graph_deprecated.h"
#include "sl12/indirect_executer.h"
#include "sl12/sampler.h"

#include "sl12/resource_streaming_texture.h"
#include "sl12/scene_mesh.h"
#include "sl12/texture_streamer.h"
#include "sl12/timestamp.h"
#include "sl12/work_graph.h"

template <typename T> using UniqueHandle = sl12::UniqueHandle<T>;

//----
struct ShaderInitDesc
{
	std::string					baseDir;
	std::vector<std::string>	includeDirs;
	sl12::ShaderPDB::Type		pdbType;
	std::string					pdbDir;
};

//----
struct WorkMaterial
{
	const sl12::ResourceItemMesh::Material*	pResMaterial;
	std::vector<sl12::ResourceHandle>		texHandles;
	int										psoType;

	bool operator==(const WorkMaterial& rhs) const
	{
		return pResMaterial == rhs.pResMaterial;
	}
	bool operator!=(const WorkMaterial& rhs) const
	{
		return !operator==(rhs);
	}

	sl12::u32 GetCurrentMiplevel() const
	{
		if (!texHandles.empty())
		{
			auto sTex = texHandles[0].GetItem<sl12::ResourceItemStreamingTexture>();
			if (sTex)
			{
				return sTex->GetCurrMipLevel();
			}
		}
		return 0;
	}
};	// struct WorkMaterial

//----
struct NeededMiplevel
{
	sl12::u32	minLevel;
	sl12::u32	latestLevel;
	int			time;
};	// struct NeededMiplevel

//----
class RenderSystem
{
public:
	RenderSystem(sl12::Device* pDev, const std::string& resDir, const ShaderInitDesc& shaderDesc);
	~RenderSystem();

	void WaitLoadAndCompile();
	
	sl12::ResourceLoader* GetResourceLoader()
	{
		return &resLoader_;
	}
	sl12::ShaderManager* GetShaderManager()
	{
		return &shaderMan_;
	}
	sl12::MeshManager* GetMeshManager()
	{
		return &meshMan_;
	}
	sl12::TextureStreamer* GetTextureStreamer()
	{
		return &texStreamer_;
	}
	sl12::CbvManager* GetCbvManager()
	{
		return &cbvMan_;
	}

	sl12::ShaderHandle GetShaderHandle(int index)
	{
		return hShaders_[index];
	}
	sl12::Shader* GetShader(int index)
	{
		return GetShaderHandle(index).GetShader();
	}

	sl12::Sampler* GetLinearWrapSampler()
	{
		return &linearWrapSampler_;
	}
	sl12::Sampler* GetLinearClampSampler()
	{
		return &linearClampSampler_;
	}
	sl12::Sampler* GetShadowSampler()
	{
		return &shadowSampler_;
	}
	
private:
	UniqueHandle<sl12::ResourceLoader>	resLoader_;
	UniqueHandle<sl12::ShaderManager>	shaderMan_;
	UniqueHandle<sl12::MeshManager>		meshMan_;
	UniqueHandle<sl12::TextureStreamer>	texStreamer_;
	UniqueHandle<sl12::CbvManager>		cbvMan_;

	// shader handles.
	std::vector<sl12::ShaderHandle>	hShaders_;

	// samplers.
	UniqueHandle<sl12::Sampler>	linearWrapSampler_;
	UniqueHandle<sl12::Sampler>	linearClampSampler_;
	UniqueHandle<sl12::Sampler>	shadowSampler_;
};	// class RenderSystem

struct TemporalCBs
{
	sl12::CbvHandle hSceneCB, hFrustumCB;
	sl12::CbvHandle hLightCB, hShadowCB;
	sl12::CbvHandle hDetailCB;
	sl12::CbvHandle hBlurXCB, hBlurYCB;
	sl12::CbvHandle hAmbOccCB;
	sl12::CbvHandle hTileCB;
	sl12::CbvHandle hDebugCB;
	std::vector<sl12::CbvHandle> hMeshCBs;

	void Clear()
	{
		hSceneCB.Reset();
		hFrustumCB.Reset();
		hLightCB.Reset();
		hShadowCB.Reset();
		hDetailCB.Reset();
		hBlurXCB.Reset();
		hBlurYCB.Reset();
		hAmbOccCB.Reset();
		hTileCB.Reset();
		hDebugCB.Reset();
		hMeshCBs.clear();
	}
};

struct RenderPassSetupDesc
{
	bool bUseVisibilityBuffer = false;
	bool bUseMeshShader = false;
	int visToGBufferType = 0;
	int ssaoType = 0;
	bool bNeedDeinterleave = false;
	
	bool operator==(const RenderPassSetupDesc& rhs) const
	{
		return (bUseVisibilityBuffer == rhs.bUseVisibilityBuffer)
			&& (bUseMeshShader == rhs.bUseMeshShader)
			&& (visToGBufferType == rhs.visToGBufferType)
			&& (ssaoType == rhs.ssaoType)
			&& (bNeedDeinterleave == rhs.bNeedDeinterleave);
	}
	bool operator!=(const RenderPassSetupDesc& rhs) const
	{
		return !operator==(rhs);
	}
};

//----
class Scene
{
public:
	Scene();
	~Scene();

	bool Initialize(sl12::Device* pDev, RenderSystem* pRenderSys, int meshType);
	void Finalize();

	void SetViewportResolution(sl12::u32 width, sl12::u32 height);
	bool CreateSceneMeshes(int meshType);
	void CreateMaterialList();
	void UpdateBindlessTextures();
	void CopyMaterialData(sl12::CommandList* pCmdList);
	void CreateMeshletBounds(sl12::CommandList* pCmdList);

	bool InitRenderPass();
	void SetupRenderPass(sl12::Texture* pSwapchainTarget, const RenderPassSetupDesc& desc);
	void LoadRenderGraphCommand();
	void ExecuteRenderGraphCommand();

	sl12::u32 GetScreenWidth() const
	{
		return screenWidth_;
	}
	sl12::u32 GetScreenHeight() const
	{
		return screenHeight_;
	}

	sl12::ResourceHandle GetSuzanneMeshHandle()
	{
		return hSuzanneMesh_;
	}
	sl12::ResourceHandle GetSponzaMeshHandle()
	{
		return hSponzaMesh_;
	}
	sl12::ResourceHandle GetCurtainMeshHandle()
	{
		return hCurtainMesh_;
	}
	sl12::ResourceHandle GetSphereMeshHandle()
	{
		return hSphereMesh_;
	}
	sl12::ResourceHandle GetDetailTexHandle()
	{
		return hDetailTex_;
	}
	sl12::ResourceHandle GetDotTexHandle()
	{
		return hDotTex_;
	}
	sl12::BufferView* GetSuzanneMeshletBV()
	{
		return &SuzanneMeshletBV_;
	}
	sl12::BufferView* GetSponzaMeshletBV()
	{
		return &SponzaMeshletBV_;
	}
	sl12::BufferView* GetCurtainMeshletBV()
	{
		return &CurtainMeshletBV_;
	}
	sl12::BufferView* GetSphereMeshletBV()
	{
		return &SphereMeshletBV_;
	}
	std::vector<std::shared_ptr<sl12::SceneMesh>>& GetSceneMeshes()
	{
		return sceneMeshes_;
	}
	void GetSceneAABB(DirectX::XMFLOAT3& aabbMin, DirectX::XMFLOAT3& aabbMax)
	{
		aabbMin = sceneAABBMin_;
		aabbMax = sceneAABBMax_;
	}
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& GetBindlessTextures()
	{
		return bindlessTextures_;
	}
	sl12::Buffer* GetMaterialDataB()
	{
		return &materialDataB_;
	}
	sl12::Buffer* GetMaterialDataCopyB()
	{
		return &materialDataCopyB_;
	}
	sl12::BufferView* GetMaterialDataBV()
	{
		return &materialDataBV_;
	}
	std::vector<WorkMaterial>& GetWorkMaterials()
	{
		return workMaterials_;
	}
	int GetMaterialIndex(const sl12::ResourceItemMesh::Material* mat);
	sl12::Buffer* GetMiplevelBuffer()
	{
		return &miplevelBuffer_;
	}
	sl12::Buffer* GetMiplevelCopySrc()
	{
		return &miplevelCopySrc_;
	}
	sl12::UnorderedAccessView* GetMiplevelUAV()
	{
		return &miplevelUAV_;
	}
	UniqueHandle<sl12::Buffer>* GetMiplevelReadbacks()
	{
		return miplevelReadbacks_;
	}
	std::vector<NeededMiplevel>& GetNeededMiplevels()
	{
		return neededMiplevels_;
	}

	TemporalCBs& GetTemporalCBs()
	{
		return tempCBs_;
	}
	std::vector<sl12::CbvHandle>& GetMeshletCBs()
	{
		return meshletCBs_;
	}
	sl12::RenderGraph* GetRenderGraph() const
	{
		return &renderGraph_;
	}

private:
	void ComputeSceneAABB();
	void SetupRenderPassGraph(const RenderPassSetupDesc& desc);

private:
	static const int kBufferCount = sl12::Swapchain::kMaxBuffer;

private:
	std::string		homeDir_;
	std::string		appShaderDir_, sysShaderInclDir_;

	sl12::Device*	pDevice_ = nullptr;
	RenderSystem*	pRenderSystem_ = nullptr;

	sl12::u32		screenWidth_, screenHeight_;
	
	// resource handles.
	sl12::ResourceHandle	hSuzanneMesh_;
	sl12::ResourceHandle	hSponzaMesh_;
	sl12::ResourceHandle	hCurtainMesh_;
	sl12::ResourceHandle	hSphereMesh_;
	sl12::ResourceHandle	hDetailTex_;
	sl12::ResourceHandle	hDotTex_;

	// meshlet bounds buffer.
	UniqueHandle<sl12::Buffer> SuzanneMeshletB_;
	UniqueHandle<sl12::Buffer> SponzaMeshletB_;
	UniqueHandle<sl12::Buffer> CurtainMeshletB_;
	UniqueHandle<sl12::Buffer> SphereMeshletB_;
	UniqueHandle<sl12::BufferView> SuzanneMeshletBV_;
	UniqueHandle<sl12::BufferView> SponzaMeshletBV_;
	UniqueHandle<sl12::BufferView> CurtainMeshletBV_;
	UniqueHandle<sl12::BufferView> SphereMeshletBV_;

	// scene meshes.
	std::vector<std::shared_ptr<sl12::SceneMesh>>	sceneMeshes_;
	DirectX::XMFLOAT3		sceneAABBMax_, sceneAABBMin_;

	// work graph resources.
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>	bindlessTextures_;
	UniqueHandle<sl12::Buffer>					materialDataB_, materialDataCopyB_;
	UniqueHandle<sl12::BufferView>				materialDataBV_;
	std::vector<WorkMaterial>					workMaterials_;

	// miplevel feedback resources.
	UniqueHandle<sl12::Buffer>				miplevelBuffer_, miplevelCopySrc_;
	UniqueHandle<sl12::UnorderedAccessView>	miplevelUAV_;
	UniqueHandle<sl12::Buffer>				miplevelReadbacks_[2];
	std::vector<NeededMiplevel>				neededMiplevels_;

	// temporal cbuffers.
	TemporalCBs tempCBs_;
	std::vector<sl12::CbvHandle> meshletCBs_;

	// render pass.
	UniqueHandle<sl12::RenderGraph>						renderGraph_;
	std::vector<std::unique_ptr<AppPassBase>>			passes_;
	std::map<AppPassType, sl12::RenderGraph::Node>		passNodes_;
	RenderPassSetupDesc									lastRenderPassDesc_;
};	// class Scene

//	EOF
