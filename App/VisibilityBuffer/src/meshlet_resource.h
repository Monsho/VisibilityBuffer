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
struct WorldMaterial
{
	const sl12::ResourceItemMesh::Material*	pResMaterial;
	std::vector<sl12::ResourceHandle>		texHandles;

	bool operator==(const WorldMaterial& rhs) const
	{
		return pResMaterial == rhs.pResMaterial;
	}
	bool operator!=(const WorldMaterial& rhs) const
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
};	// struct WorldMaterial

//----
struct SubmeshInfo
{
	sl12::u32					submeshIndex;
	sl12::u32					materialIndex;
};	// struct SubmeshInfo

//----
struct MeshInstanceInfo
{
	std::weak_ptr<sl12::SceneMesh>	meshInstance;
	sl12::u32						argIndex[2]; // 0:opaque, 1:masked
};	// struct MeshInstanceInfo

//----
struct MeshResInfo
{
	const sl12::ResourceItemMesh*	resMesh;
	sl12::u32						submeshCount[2];	// 0:opaque, 1:masked
	sl12::u32						meshletCount[2];	// 0:opaque, 1:masked
	std::vector<SubmeshInfo>		nonXluSubmeshInfos;	// 非半透明（Opaque & Masked）のサブメッシュ情報
	std::vector<sl12::u32>			xluSubmeshIndices;	// 半透明サブメッシュのインデックス
};

//----
class MeshletResource
{
public:
	MeshletResource();
	~MeshletResource();

	void CreateResources(sl12::Device* pDev, const std::vector<std::shared_ptr<sl12::SceneMesh>>& meshes);

	void CopyMeshletBounds(sl12::CommandList* pCmdList);

	void UpdateBindlessTextures(sl12::Device* pDev);

	const std::vector<WorldMaterial>& GetWorldMaterials() const
	{
		return worldMaterials_;
	}
	const MeshResInfo* GetMeshResInfo(const sl12::ResourceItemMesh* resMesh) const
	{
		auto it = meshResInfos_.find(resMesh);
		if (it == meshResInfos_.end())
		{
			return nullptr;
		}
		return &it->second;
	}
	const std::vector<MeshInstanceInfo>& GetMeshInstanceInfos() const
	{
		return meshInstanceInfos_;
	}

	const sl12::Buffer* GetMeshletIndirectArgUpload() const
	{
		return &meshletIndirectArgUpload_;
	}
	const sl12::Buffer* GetInstanceUpload() const
	{
		return &instanceUpload_;
	}
	const sl12::Buffer* GetSubmeshUpload() const
	{
		return &submeshUpload_;
	}
	const sl12::Buffer* GetMeshletUpload() const
	{
		return &meshletUpload_;
	}
	const sl12::Buffer* GetDrawCallUpload() const
	{
		return &drawcallUpload_;
	}
	const sl12::BufferView* GetMeshletBoundsSRV(const sl12::ResourceItemMesh* resMesh) const
	{
		auto it = meshletBoundsSRVs_.find(resMesh);
		if (it == meshletBoundsSRVs_.end())
		{
			return nullptr;
		}
		return &it->second;
	}
	const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& GetBindlessTextures() const
	{
		return bindlessTextures_;
	}
	const sl12::Buffer* GetMaterialDataUpload() const
	{
		return &materialDataUpload_;
	}

private:
	void CreateMeshletBoundsResources(sl12::Device* pDev);
	int GetWorldMaterialIndex(const sl12::ResourceItemMesh::Material* mat);
	void CreateVisibilityResources(sl12::Device* pDev);
	void CreateWorkGraphResources(sl12::Device* pDev);
	
private:
	bool								bNeedCopy = false;
	std::vector<WorldMaterial>			worldMaterials_;
	std::map<const sl12::ResourceItemMesh*, MeshResInfo>	meshResInfos_;
	std::vector<MeshInstanceInfo>		meshInstanceInfos_;

	// MeshletのDrawIndirect引数バッファ
	// メッシュインスタンスのOpaque/MaskedのMeshletの数だけ生成
	UniqueHandle<sl12::Buffer>			meshletIndirectArgUpload_;
	// メッシュインスタンスバッファ
	// メッシュインスタンスの行列など、インスタンスに関わる情報を持つ
	UniqueHandle<sl12::Buffer>			instanceUpload_;
	// サブメッシュバッファ
	// ResMeshが持つサブメッシュデータの合計数
	// 同一ResMeshを持つインスタンスは1つにまとめる
	UniqueHandle<sl12::Buffer>			submeshUpload_;
	// メッシュレットバッファ
	// サブメッシュバッファに保存したサブメッシュが持つメッシュレットの合計数
	UniqueHandle<sl12::Buffer>			meshletUpload_;
	// ドローコールバッファ
	// メッシュインスタンスのOpaque/Maskedのメッシュレットの合計数
	// ResMeshごとではなく、インスタンスごとであることに注意
	UniqueHandle<sl12::Buffer>			drawcallUpload_;
	// メッシュレットバウンズバッファ
	// メッシュリソースごとのメッシュレットバウンズのリスト
	std::map<const sl12::ResourceItemMesh*, UniqueHandle<sl12::Buffer>>		meshletBoundsUploads_;
	std::map<const sl12::ResourceItemMesh*, UniqueHandle<sl12::Buffer>>		meshletBoundsBuffers_;
	std::map<const sl12::ResourceItemMesh*, UniqueHandle<sl12::BufferView>>	meshletBoundsSRVs_;
	// WorkGraph関連データ
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>	bindlessTextures_;
	UniqueHandle<sl12::Buffer>					materialDataUpload_;
};

//	EOF
