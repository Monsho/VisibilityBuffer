#include "meshlet_resource.h"
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


MeshletResource::MeshletResource()
	: bNeedCopy(false)
{}

MeshletResource::~MeshletResource()
{
	worldMaterials_.clear();
	meshInstanceInfos_.clear();
	meshletIndirectArgUpload_.Reset();
	instanceUpload_.Reset();
	submeshUpload_.Reset();
	meshletUpload_.Reset();
	drawcallUpload_.Reset();
}

void MeshletResource::CreateResources(sl12::Device* pDev, const std::vector<std::shared_ptr<sl12::SceneMesh>>& meshes)
{
	worldMaterials_.clear();
	meshResInfos_.clear();
	meshInstanceInfos_.clear();

	// メッシュリソースごとの情報を収集する
	for (auto&& mesh : meshes)
	{
		auto resMesh = mesh->GetParentResource();
		if (meshResInfos_.find(resMesh) == meshResInfos_.end())
		{
			MeshResInfo resInfo = {};
			resInfo.resMesh = resMesh;

			// マテリアルごとにSubmeshをOpaque/Masked/Translucentに分ける
			std::vector<sl12::u32> opaqueSubmesh, maskedSubmesh, xluSubmesh;
			auto&& submeshes = resMesh->GetSubmeshes();
			auto&& materials = resMesh->GetMaterials();
			sl12::u32 submeshIndex = 0;
			for (auto&& submesh : submeshes)
			{
				auto&& material = materials[submesh.materialIndex];
				if (material.blendType == sl12::ResourceMeshMaterialBlendType::Opaque)
				{
					opaqueSubmesh.push_back(submeshIndex);
				}
				else if (material.blendType == sl12::ResourceMeshMaterialBlendType::Masked)
				{
					maskedSubmesh.push_back(submeshIndex);
				}
				else
				{
					xluSubmesh.push_back(submeshIndex);
				}
				submeshIndex++;

				// World Materialは不透明・半透明関係なく登録
				auto it = std::find_if(
					worldMaterials_.begin(), worldMaterials_.end(),
					[&material](const WorldMaterial& rhs) { return rhs.pResMaterial == &material; });
				if (it == worldMaterials_.end())
				{
					WorldMaterial mat;
					mat.pResMaterial = &material;
					mat.texHandles.push_back(mat.pResMaterial->baseColorTex);
					mat.texHandles.push_back(mat.pResMaterial->normalTex);
					mat.texHandles.push_back(mat.pResMaterial->ormTex);
					worldMaterials_.push_back(mat);
				}
			}

			// サブメッシュ情報、及びワールド全体のマテリアルリストを収集する関数
			auto GatherSubmeshInfo = [&](const std::vector<sl12::u32>& submeshIndices, int type)
			{
				for (auto index : submeshIndices)
				{
					auto&& submesh = submeshes[index];
					auto&& material = materials[submesh.materialIndex];

					auto it = std::find_if(
						worldMaterials_.begin(), worldMaterials_.end(),
						[&material](const WorldMaterial& rhs) { return rhs.pResMaterial == &material; });
					sl12::u32 worldMatIndex = (sl12::u32)std::distance(worldMaterials_.begin(), it);

					SubmeshInfo submeshInfo = {};
					submeshInfo.submeshIndex = index;
					submeshInfo.materialIndex = worldMatIndex;
					
					resInfo.meshletCount[type] += static_cast<sl12::u32>(submesh.meshlets.size());
					resInfo.nonXluSubmeshInfos.push_back(submeshInfo);
				}
				resInfo.submeshCount[type] = (sl12::u32)submeshIndices.size();
			};
			// Opaqueの収集
			GatherSubmeshInfo(opaqueSubmesh, 0);
			// Maskedの収集
			GatherSubmeshInfo(maskedSubmesh, 1);

			// 半透明サブメッシュリストのコピー
			resInfo.xluSubmeshIndices = xluSubmesh;

			// 登録
			meshResInfos_[resMesh] = resInfo;
		}
	}
	
	// メッシュインスタンスごとの情報を収集する
	sl12::u32 argCount = 0;
	for (auto&& mesh : meshes)
	{
		auto resMesh = mesh->GetParentResource();
		
		MeshInstanceInfo meshInfo = {};
		meshInfo.meshInstance = mesh;
		meshInfo.argIndex[0] = argCount;
		meshInfo.argIndex[1] = argCount + meshResInfos_[resMesh].meshletCount[0];

		meshInstanceInfos_.push_back(meshInfo);

		argCount += meshResInfos_[resMesh].meshletCount[0] + meshResInfos_[resMesh].meshletCount[1];
	}

	// DrawIndirectArgのアップロードバッファ生成
	{
		meshletIndirectArgUpload_ = sl12::MakeUnique<sl12::Buffer>(pDev);

		sl12::BufferDesc desc{};
		desc.stride = kIndirectArgsBufferStride;
		desc.size = desc.stride * argCount + 4/* overflow support. */;
		desc.usage = sl12::ResourceUsage::Unknown;
		desc.heap = sl12::BufferHeap::Dynamic;
		desc.initialState = D3D12_RESOURCE_STATE_COMMON;
		meshletIndirectArgUpload_->Initialize(pDev, desc);

		sl12::u32* p = (sl12::u32*)meshletIndirectArgUpload_->Map();
		sl12::u32 index = 0;
		for (auto&& meshInfo : meshInstanceInfos_)
		{
			auto resMesh = meshInfo.meshInstance.lock()->GetParentResource();
			const MeshResInfo& resInfo = meshResInfos_[resMesh];
			auto&& submeshes = resMesh->GetSubmeshes();

			for (auto&& submeshInfo : resInfo.nonXluSubmeshInfos)
			{
				auto&& submesh = submeshes[submeshInfo.submeshIndex];
				UINT StartIndexLocation = (UINT)(submesh.indexOffsetBytes / sl12::ResourceItemMesh::GetIndexStride());
				int BaseVertexLocation = (int)(submesh.positionOffsetBytes / sl12::ResourceItemMesh::GetPositionStride());

				for (auto&& meshlet : submesh.meshlets)
				{
					*p++ = index;

					D3D12_DRAW_INDEXED_ARGUMENTS* arg = (D3D12_DRAW_INDEXED_ARGUMENTS*)p;
					arg->IndexCountPerInstance = meshlet.indexCount;
					arg->InstanceCount = 1;
					arg->StartIndexLocation = StartIndexLocation + meshlet.indexOffset;
					arg->BaseVertexLocation = BaseVertexLocation;
					arg->StartInstanceLocation = 0;
					
					p = (sl12::u32*)(arg + 1);
					index++;
				}
			}
		}
		meshletIndirectArgUpload_->Unmap();
	}

	// メッシュレットバウンズバッファ生成
	CreateMeshletBoundsResources(pDev);
	
	// VisibilityBuffer用のアップロードバッファ生成
	CreateVisibilityResources(pDev);

	// WorkGraph用のアップロードバッファ生成
	CreateWorkGraphResources(pDev);
}

void MeshletResource::CreateMeshletBoundsResources(sl12::Device* pDev)
{
	for (auto&& resInfo : meshResInfos_)
	{
		auto resMesh = resInfo.first;
		auto&& submeshes = resMesh->GetSubmeshes();

		sl12::u32 meshletTotal = resInfo.second.meshletCount[0] + resInfo.second.meshletCount[1];

		// create buffers.
		UniqueHandle<sl12::Buffer> B = sl12::MakeUnique<sl12::Buffer>(pDev);
		UniqueHandle<sl12::Buffer> U = sl12::MakeUnique<sl12::Buffer>(pDev);
		UniqueHandle<sl12::BufferView> BV = sl12::MakeUnique<sl12::BufferView>(pDev);
		
		sl12::BufferDesc desc{};
		desc.heap = sl12::BufferHeap::Default;
		desc.stride = sizeof(MeshletBound);
		desc.size = desc.stride * meshletTotal;
		desc.usage = sl12::ResourceUsage::ShaderResource;
		desc.initialState = D3D12_RESOURCE_STATE_COMMON;

		B->Initialize(pDev, desc);
		BV->Initialize(pDev, &B, 0, 0, (sl12::u32)desc.stride);

		desc.heap = sl12::BufferHeap::Dynamic;
		U->Initialize(pDev, desc);

		// fill upload buffer.
		MeshletBound* pBound = static_cast<MeshletBound*>(U->Map());
		for (auto&& info : resInfo.second.nonXluSubmeshInfos)
		{
			auto&& submesh = submeshes[info.submeshIndex];
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
		U->Unmap();

		// マップに登録
		meshletBoundsUploads_[resMesh] = std::move(U);
		meshletBoundsBuffers_[resMesh] = std::move(B);
		meshletBoundsSRVs_[resMesh] = std::move(BV);
	}
}

int MeshletResource::GetWorldMaterialIndex(const sl12::ResourceItemMesh::Material* mat)
{
	auto it = std::find_if(
		worldMaterials_.begin(), worldMaterials_.end(),
		[mat](const WorldMaterial& rhs) { return rhs.pResMaterial == mat; });
	if (it == worldMaterials_.end())
	{
		return -1;
	}
	auto index = std::distance(worldMaterials_.begin(), it);
	return (int)index;
}

void MeshletResource::CreateVisibilityResources(sl12::Device* pDev)
{
	struct Data
	{
		std::map<const sl12::ResourceItemMesh*, sl12::u32> meshMap;
		std::vector<const sl12::ResourceItemMesh*> meshList;
		std::vector<sl12::u32> submeshOffsets;
		std::vector<sl12::u32> meshletOffsets;
		sl12::u32 meshCount = 0;
		sl12::u32 submeshCount = 0;
		sl12::u32 meshletCount = 0;
		sl12::u32 drawCallCount = 0;
	};

	// メッシュリソース、及びインスタンスの各種情報を統合する
	Data data;
	for (auto&& resInfo : meshResInfos_)
	{
		auto resMesh = resInfo.first;
		data.meshMap[resMesh] = data.meshCount++;
		data.meshList.push_back(resMesh);
		data.submeshOffsets.push_back(data.submeshCount);
		data.submeshCount += (sl12::u32)resInfo.second.nonXluSubmeshInfos.size();

		auto&& submeshes = resMesh->GetSubmeshes();
		for (auto& submeshInfo : resInfo.second.nonXluSubmeshInfos)
		{
			data.meshletOffsets.push_back(data.meshletCount);
			data.meshletCount += (sl12::u32)submeshes[submeshInfo.submeshIndex].meshlets.size();
		}
	}
	for (auto&& instanceInfo : meshInstanceInfos_)
	{
		auto resMesh = instanceInfo.meshInstance.lock()->GetParentResource();
		const MeshResInfo& resInfo = meshResInfos_[resMesh];
		data.drawCallCount += resInfo.meshletCount[0] + resInfo.meshletCount[1];
	}

	// バッファ生成
	instanceUpload_ = sl12::MakeUnique<sl12::Buffer>(pDev);
	submeshUpload_ = sl12::MakeUnique<sl12::Buffer>(pDev);
	meshletUpload_ = sl12::MakeUnique<sl12::Buffer>(pDev);
	drawcallUpload_ = sl12::MakeUnique<sl12::Buffer>(pDev);

	{
		sl12::BufferDesc desc;
		desc.InitializeStructured(sizeof(InstanceData), meshInstanceInfos_.size(), sl12::ResourceUsage::Unknown, sl12::BufferHeap::Dynamic);
		instanceUpload_->Initialize(pDev, desc);
	}
	{
		sl12::BufferDesc desc;
		desc.InitializeStructured(sizeof(SubmeshData), data.submeshCount, sl12::ResourceUsage::Unknown, sl12::BufferHeap::Dynamic);
		submeshUpload_->Initialize(pDev, desc);
	}
	{
		sl12::BufferDesc desc;
		desc.InitializeStructured(sizeof(MeshletData), data.meshletCount, sl12::ResourceUsage::Unknown, sl12::BufferHeap::Dynamic);
		meshletUpload_->Initialize(pDev, desc);
	}
	{
		sl12::BufferDesc desc;
		desc.InitializeStructured(sizeof(DrawCallData), data.drawCallCount, sl12::ResourceUsage::Unknown, sl12::BufferHeap::Dynamic);
		drawcallUpload_->Initialize(pDev, desc);
	}

	// アップロードバッファの更新
	InstanceData* instanceData = (InstanceData*)instanceUpload_->Map();
	SubmeshData* submeshData = (SubmeshData*)submeshUpload_->Map();
	MeshletData* meshletData = (MeshletData*)meshletUpload_->Map();
	DrawCallData* drawcallData = (DrawCallData*)drawcallUpload_->Map();

	// メッシュリソースごとのアップロードバッファを更新
	sl12::u32 submeshTotal = 0;
	for (auto resMesh : data.meshList)
	{
		auto meshInfo = std::find_if(meshInstanceInfos_.begin(), meshInstanceInfos_.end(),
			[resMesh](const MeshInstanceInfo& rhs) { return rhs.meshInstance.lock()->GetParentResource() == resMesh; });
		assert(meshInfo != meshInstanceInfos_.end());
		const MeshResInfo& resInfo = meshResInfos_[resMesh];
		auto&& submeshes = resMesh->GetSubmeshes();
		for (auto&& submeshInfo : resInfo.nonXluSubmeshInfos)
		{
			auto&& submesh = submeshes[submeshInfo.submeshIndex];
			sl12::u32 submeshIndexOffset = (sl12::u32)(resMesh->GetIndexHandle().offset + submesh.indexOffsetBytes);
			submeshData->materialIndex = GetWorldMaterialIndex(&resMesh->GetMaterials()[submesh.materialIndex]);
			submeshData->posOffset = (sl12::u32)(resMesh->GetPositionHandle().offset + submesh.positionOffsetBytes);
			submeshData->normalOffset = (sl12::u32)(resMesh->GetNormalHandle().offset + submesh.normalOffsetBytes);
			submeshData->tangentOffset = (sl12::u32)(resMesh->GetTangentHandle().offset + submesh.tangentOffsetBytes);
			submeshData->uvOffset = (sl12::u32)(resMesh->GetTexcoordHandle().offset + submesh.texcoordOffsetBytes);
			submeshData->indexOffset = submeshIndexOffset;
			submeshData++;

			sl12::u32 submeshPackedPrimOffset = (sl12::u32)(resMesh->GetMeshletPackedPrimHandle().offset + submesh.meshletPackedPrimOffsetBytes);
			sl12::u32 submeshVertexIndexOffset = (sl12::u32)(resMesh->GetMeshletVertexIndexHandle().offset + submesh.meshletVertexIndexOffsetBytes);
			for (auto&& meshlet : submesh.meshlets)
			{
				meshletData->submeshIndex = submeshTotal;
				meshletData->indexOffset = submeshIndexOffset + meshlet.indexOffset * (sl12::u32)sl12::ResourceItemMesh::GetIndexStride();
				meshletData->meshletPackedPrimCount = meshlet.primitiveCount;
				meshletData->meshletPackedPrimOffset = submeshPackedPrimOffset + meshlet.primitiveOffset * (sl12::u32)sizeof(sl12::u32);
				meshletData->meshletVertexIndexCount = meshlet.vertexIndexCount;
				meshletData->meshletVertexIndexOffset = submeshVertexIndexOffset + meshlet.vertexIndexOffset * (sl12::u32)sl12::ResourceItemMesh::GetIndexStride();
				meshletData++;
			}

			submeshTotal++;
		}
	}
	// メッシュインスタンスごとのアップロードバッファを更新
	sl12::u32 instanceIndex = 0;
	for (auto&& meshInfo : meshInstanceInfos_)
	{
		auto mesh = meshInfo.meshInstance.lock();
		auto resMesh = mesh->GetParentResource();
		auto meshIndex = data.meshMap[resMesh];
		auto submeshOffset = data.submeshOffsets[meshIndex];
		const MeshResInfo& resInfo = meshResInfos_[resMesh];
		
		// set mesh constant.
		DirectX::XMMATRIX l2w = DirectX::XMLoadFloat4x4(&mesh->GetMtxLocalToWorld());
		DirectX::XMMATRIX w2l = DirectX::XMMatrixInverse(nullptr, l2w);
		instanceData->mtxBoxTransform = mesh->GetParentResource()->GetMtxBoxToLocal();
		instanceData->mtxLocalToWorld = mesh->GetMtxLocalToWorld();
		DirectX::XMStoreFloat4x4(&instanceData->mtxWorldToLocal, w2l);
		instanceData++;

		auto&& submeshes = resMesh->GetSubmeshes();
		sl12::u32 submesh_count = (sl12::u32)resInfo.nonXluSubmeshInfos.size();
		for (sl12::u32 i = 0; i < submesh_count; i++)
		{
			auto&& submesh = submeshes[resInfo.nonXluSubmeshInfos[i].submeshIndex];
			auto meshletOffset = data.meshletOffsets[submeshOffset + i];
			sl12::u32 meshlet_count = (sl12::u32)submesh.meshlets.size();
			for (sl12::u32 j = 0; j < meshlet_count; j++)
			{
				drawcallData->instanceIndex = instanceIndex;
				drawcallData->meshletIndex = meshletOffset + j;
				drawcallData++;
			}
		}

		instanceIndex++;
	}

	instanceUpload_->Unmap();
	submeshUpload_->Unmap();
	meshletUpload_->Unmap();
	drawcallUpload_->Unmap();
}

void MeshletResource::CopyMeshletBounds(sl12::CommandList* pCmdList)
{
	for (auto&& upload : meshletBoundsUploads_)
	{
		auto resMesh = upload.first;
		pCmdList->GetLatestCommandList()->CopyResource(meshletBoundsBuffers_[resMesh]->GetResourceDep(), upload.second->GetResourceDep());
	}
	meshletBoundsUploads_.clear();
}

void MeshletResource::UpdateBindlessTextures(sl12::Device* pDev)
{
	bindlessTextures_.clear();
	bindlessTextures_.push_back(pDev->GetDummyTextureView(sl12::DummyTex::Black)->GetDescInfo().cpuHandle);
	bindlessTextures_.push_back(pDev->GetDummyTextureView(sl12::DummyTex::FlatNormal)->GetDescInfo().cpuHandle);
	for (auto&& mat : worldMaterials_)
	{
		// each textures.
		auto resTex = mat.pResMaterial->baseColorTex.GetItem<sl12::ResourceItemTextureBase>();
		if (resTex)
		{
			bindlessTextures_.push_back(const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView().GetDescInfo().cpuHandle);
		}
		resTex = mat.pResMaterial->normalTex.GetItem<sl12::ResourceItemTextureBase>();
		if (resTex)
		{
			bindlessTextures_.push_back(const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView().GetDescInfo().cpuHandle);
		}
		resTex = mat.pResMaterial->ormTex.GetItem<sl12::ResourceItemTextureBase>();
		if (resTex)
		{
			bindlessTextures_.push_back(const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView().GetDescInfo().cpuHandle);
		}
		resTex = mat.pResMaterial->emissiveTex.GetItem<sl12::ResourceItemTextureBase>();
		if (resTex)
		{
			bindlessTextures_.push_back(const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView().GetDescInfo().cpuHandle);
		}
	}
}

void MeshletResource::CreateWorkGraphResources(sl12::Device* pDev)
{
	sl12::BufferDesc desc{};
	desc.heap = sl12::BufferHeap::Dynamic;
	desc.usage = sl12::ResourceUsage::ShaderResource;
	desc.stride = sizeof(MaterialData);
	desc.size = desc.stride * worldMaterials_.size();
	desc.initialState = D3D12_RESOURCE_STATE_COMMON;

	materialDataUpload_ = sl12::MakeUnique<sl12::Buffer>(pDev);
	materialDataUpload_->Initialize(pDev, desc);

	MaterialData* data = static_cast<MaterialData*>(materialDataUpload_->Map());
	bindlessTextures_.clear();
	bindlessTextures_.push_back(pDev->GetDummyTextureView(sl12::DummyTex::Black)->GetDescInfo().cpuHandle);
	bindlessTextures_.push_back(pDev->GetDummyTextureView(sl12::DummyTex::FlatNormal)->GetDescInfo().cpuHandle);
	for (auto&& mat : worldMaterials_)
	{
		data->shaderIndex = 0;
		// default.
		data->colorTexIndex = data->ormTexIndex = 0;
		data->normalTexIndex = 1;
		// each textures.
		auto resTex = mat.pResMaterial->baseColorTex.GetItem<sl12::ResourceItemTextureBase>();
		if (resTex)
		{
			data->colorTexIndex = (UINT)bindlessTextures_.size();
			bindlessTextures_.push_back(const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView().GetDescInfo().cpuHandle);
		}
		resTex = mat.pResMaterial->normalTex.GetItem<sl12::ResourceItemTextureBase>();
		if (resTex)
		{
			data->normalTexIndex = (UINT)bindlessTextures_.size();
			bindlessTextures_.push_back(const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView().GetDescInfo().cpuHandle);
		}
		resTex = mat.pResMaterial->ormTex.GetItem<sl12::ResourceItemTextureBase>();
		if (resTex)
		{
			data->ormTexIndex = (UINT)bindlessTextures_.size();
			bindlessTextures_.push_back(const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView().GetDescInfo().cpuHandle);
		}
		resTex = mat.pResMaterial->emissiveTex.GetItem<sl12::ResourceItemTextureBase>();
		if (resTex)
		{
			data->emissiveTexIndex = (UINT)bindlessTextures_.size();
			bindlessTextures_.push_back(const_cast<sl12::ResourceItemTextureBase*>(resTex)->GetTextureView().GetDescInfo().cpuHandle);
		}
		data++;
	}
	materialDataUpload_->Unmap();
}


//	EOF
