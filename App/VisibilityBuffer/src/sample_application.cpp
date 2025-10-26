#include "sample_application.h"
#include "shader_types.h"

#include "sl12/resource_mesh.h"
#include "sl12/string_util.h"
#include "sl12/root_signature.h"
#include "sl12/descriptor_set.h"
#include "sl12/resource_texture.h"
#include "sl12/command_queue.h"
#include "sl12/resource_streaming_texture.h"

#define NOMINMAX
#include <windowsx.h>
#include <memory>
#include <random>

#include "../shaders/constant_defs.h"
#define USE_IN_CPP
#include <Shlwapi.h>

#include "../shaders/cbuffer.hlsli"

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

	std::string CreateTimestampedFilename(const std::string& originalFilename)
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t now_c = std::chrono::system_clock::to_time_t(now);

		std::tm local_tm{};
		errno_t err = localtime_s(&local_tm, &now_c);

		std::stringstream ss;
		ss << std::put_time(&local_tm, "%Y%m%d_%H%M%S");
		std::string timestampStr = ss.str();

		const size_t dotPos = originalFilename.find_last_of('.');

		if (dotPos == std::string::npos)
		{
			return originalFilename + "_" + timestampStr;
		}
		else
		{
			return originalFilename.substr(0, dotPos) + "_" + timestampStr + originalFilename.substr(dotPos);
		}
	}
}

SampleApplication::SampleApplication(HINSTANCE hInstance, int nCmdShow, int screenWidth, int screenHeight, sl12::ColorSpaceType csType, const std::string& homeDir, int meshType, const std::string& appShader, const std::string& sysShader)
	: Application(hInstance, nCmdShow, screenWidth, screenHeight, csType)
	, displayWidth_(screenWidth), displayHeight_(screenHeight)
	, meshType_(meshType)
{
	std::filesystem::path p(homeDir);
	p = std::filesystem::absolute(p);
	homeDir_ = p.string();

	appShaderDir_ = appShader.empty() ? kShaderDir : appShader;
	sysShaderInclDir_ = sysShader.empty() ? kShaderIncludeDir : sysShader;
}

SampleApplication::~SampleApplication()
{}

bool SampleApplication::Initialize()
{
	// init render system.
	const std::string resDir = sl12::JoinPath(homeDir_, kResourceDir);
	ShaderInitDesc shaderDesc{};
	shaderDesc.baseDir = sl12::JoinPath(homeDir_, appShaderDir_);
	shaderDesc.includeDirs.push_back(sl12::JoinPath(homeDir_, sysShaderInclDir_));
	shaderDesc.pdbDir = sl12::JoinPath(homeDir_, kShaderPDBDir);
	shaderDesc.pdbType = sl12::ShaderPDB::None;
#if 1 // if 1, output shader debug files
	shaderDesc.pdbType = sl12::ShaderPDB::Full;
#endif
	renderSys_ = sl12::MakeUnique<RenderSystem>(nullptr, &device_, resDir, shaderDesc);

	// init scene.
	scene_ = sl12::MakeUnique<Scene>(nullptr);
	if (!scene_->Initialize(&device_, &renderSys_, meshType_))
	{
		sl12::ConsolePrint("Error: Failed to init scene.\n");
		return false;
	}
	scene_->SetViewportResolution(screenWidth_, screenHeight_);

	// init command list.
	mainCmdList_ = sl12::MakeUnique<CommandLists>(nullptr);
	if (!mainCmdList_->Initialize(&device_, &device_.GetGraphicsQueue()))
	{
		sl12::ConsolePrint("Error: failed to init main command list.");
		return false;
	}
	frameStartCmdlist_ = sl12::MakeUnique<CommandLists>(nullptr);
	if (!frameStartCmdlist_->Initialize(&device_, &device_.GetGraphicsQueue()))
	{
		sl12::ConsolePrint("Error: failed to init frame start command list.");
		return false;
	}
	frameEndCmdList_ = sl12::MakeUnique<CommandLists>(nullptr);
	if (!frameEndCmdList_->Initialize(&device_, &device_.GetGraphicsQueue()))
	{
		sl12::ConsolePrint("Error: failed to init frame end command list.");
		return false;
	}

	// init render graph.
	renderGraphDeprecated_ = sl12::MakeUnique<sl12::RenderGraph_Deprecated>(nullptr);

	// wait compile and load.
	renderSys_->WaitLoadAndCompile();

	// init render graph.
	scene_->InitRenderPass();

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

	// create scene meshes.
	scene_->CreateSceneMeshes(meshType_);

	// create meshlet bounds buffers.
	scene_->CreateMeshletBounds(&utilCmdList);

	// execute utility commands.
	utilCmdList->Close();
	utilCmdList->Execute();
	device_.WaitDrawDone();

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
	gui_.Reset();
	renderGraphDeprecated_.Reset();
	mainCmdList_.Reset();

	scene_.Reset();
	renderSys_.Reset();
}


void SampleApplication::SetupConstantBuffers(TemporalCBs& OutCBs)
{
	auto cbvMan = renderSys_->GetCbvManager();
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
			DirectX::XMStoreFloat4x4(&cbScene.mtxPrevProjToProj, U);
		}
		else
		{
			auto mtxClipToPrevClip = mtxClipToWorld * mtxPrevWorldToClip_;
			auto mtxPrevClipToClip = DirectX::XMMatrixInverse(nullptr, mtxClipToPrevClip);
			DirectX::XMStoreFloat4x4(&cbScene.mtxProjToPrevProj, mtxClipToPrevClip);
			DirectX::XMStoreFloat4x4(&cbScene.mtxPrevViewToProj, mtxPrevViewToClip_);
			DirectX::XMStoreFloat4x4(&cbScene.mtxPrevProjToProj, mtxPrevClipToClip);
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

		OutCBs.hSceneCB = cbvMan->GetTemporal(&cbScene, sizeof(cbScene));

		FrustumCB cbFrustum;
		sl12::CalcFrustumPlanes(mtxWorldToClip, true, true, cbFrustum.frustumPlanes);
		OutCBs.hFrustumCB = cbvMan->GetTemporal(&cbFrustum, sizeof(cbFrustum));

		mtxPrevWorldToView_ = mtxWorldToView;
		mtxPrevWorldToClip_ = mtxWorldToClip;
		mtxPrevViewToClip_ = mtxViewToClip;
	}
	{
		LightCB cbLight;

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

		OutCBs.hLightCB = cbvMan->GetTemporal(&cbLight, sizeof(cbLight));

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

		DirectX::XMFLOAT3 sceneAABBMin, sceneAABBMax;
		scene_->GetSceneAABB(sceneAABBMin, sceneAABBMax);
		DirectX::XMVECTOR pnts[] = {
			DirectX::XMVectorSet(sceneAABBMax.x, sceneAABBMax.y, sceneAABBMax.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMax.x, sceneAABBMax.y, sceneAABBMin.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMax.x, sceneAABBMin.y, sceneAABBMax.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMax.x, sceneAABBMin.y, sceneAABBMin.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMin.x, sceneAABBMax.y, sceneAABBMax.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMin.x, sceneAABBMax.y, sceneAABBMin.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMin.x, sceneAABBMin.y, sceneAABBMax.z, 1.0f),
			DirectX::XMVectorSet(sceneAABBMin.x, sceneAABBMin.y, sceneAABBMin.z, 1.0f),
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

		OutCBs.hShadowCB = cbvMan->GetTemporal(&cbShadow, sizeof(cbShadow));
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
		OutCBs.hBlurXCB = cbvMan->GetTemporal(&cbBlur, sizeof(cbBlur));

		cbBlur.offset = DirectX::XMFLOAT2(0.0f, 1.5f / (float)displayHeight_);
		OutCBs.hBlurYCB = cbvMan->GetTemporal(&cbBlur, sizeof(cbBlur));
	}
	{
		DetailCB cbDetail;

		cbDetail.detailType = detailType_;
		cbDetail.detailTile = DirectX::XMFLOAT2(detailTile_, detailTile_);
		cbDetail.detailIntensity = detailIntensity_;
		cbDetail.triplanarType = triplanarType_;
		cbDetail.triplanarTile = triplanarTile_;

		OutCBs.hDetailCB = cbvMan->GetTemporal(&cbDetail, sizeof(cbDetail));
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

		OutCBs.hAmbOccCB = cbvMan->GetTemporal(&cbAO, sizeof(cbAO));
	}
	{
		TileCB cbTile;

		UINT x = (displayWidth_ + CLASSIFY_TILE_WIDTH - 1) / CLASSIFY_TILE_WIDTH;
		UINT y = (displayHeight_ + CLASSIFY_TILE_WIDTH - 1) / CLASSIFY_TILE_WIDTH;
		cbTile.numX = x;
		cbTile.numY = y;
		cbTile.tileMax = x * y;
		cbTile.materialMax = (sl12::u32)scene_->GetMeshletResource()->GetWorldMaterials().size();

		OutCBs.hTileCB = cbvMan->GetTemporal(&cbTile, sizeof(cbTile));
	}
	{
		DebugCB cbDebug;

		cbDebug.displayMode = displayMode_;

		OutCBs.hDebugCB = cbvMan->GetTemporal(&cbDebug, sizeof(cbDebug));
	}
}

bool SampleApplication::Execute()
{
	const int kSwapchainBufferOffset = 1;
	auto frameIndex = (device_.GetSwapchain().GetFrameIndex() + sl12::Swapchain::kMaxBuffer - 1) % sl12::Swapchain::kMaxBuffer;
	auto prevFrameIndex = (device_.GetSwapchain().GetFrameIndex() + sl12::Swapchain::kMaxBuffer - 2) % sl12::Swapchain::kMaxBuffer;
	auto pFrameStartCmdList = &frameStartCmdlist_->Reset();
	auto pFrameEndCmdList = &frameEndCmdList_->Reset();
	auto* pTimestamp = timestamps_ + timestampIndex_;
	auto pSwapchainTarget = device_.GetSwapchain().GetCurrentTexture(kSwapchainBufferOffset);

	sl12::CpuTimer now = sl12::CpuTimer::CurrentTime();
	sl12::CpuTimer delta = now - currCpuTime_;
	currCpuTime_ = now;

	// control camera.
	ControlCamera(delta.ToSecond());

	// setup imgui.
	gui_->BeginNewFrame(pFrameStartCmdList, displayWidth_, displayHeight_, inputData_);
	inputData_.Reset();
	{
		bool bPrevMode = bEnableVisibilityBuffer_;

		// rendering settings.
		if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Checkbox("Visibility Buffer", &bEnableVisibilityBuffer_))
			{}

			if (bEnableVisibilityBuffer_)
			{
				if (ImGui::Checkbox("Mesh Shader for VisRender", &bEnableMeshShader_))
				{}

				static const char* kVisToGBTypes[] = {
					"Depth & Tile",
					"Compute Pixel",
					"Compute Tile",
					"Work Graph",
				};
				ImGui::Combo("Vis to GBuffer", &VisToGBufferType_, kVisToGBTypes, ARRAYSIZE(kVisToGBTypes));
			}
		}

		// light settings.
		if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat("Ambient Intensity", &ambientIntensity_, 0.0f, 10.0f);
			ImGui::SliderFloat("Directional Theta", &directionalTheta_, 0.0f, 90.0f);
			ImGui::SliderFloat("Directional Phi", &directionalPhi_, 0.0f, 360.0f);
			ImGui::ColorEdit3("Directional Color", directionalColor_);
			ImGui::SliderFloat("Directional Intensity", &directionalIntensity_, 0.0f, 10.0f);
		}

		// shadow settings.
		if (ImGui::CollapsingHeader("Shadow", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Blur", &evsmBlur_);
			ImGui::SliderFloat("Exponent", &shadowExponent_, 0.1f, 50.0f);
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
			ImGui::SliderFloat("GI Intensity", &ssgiIntensity_, 0.0f, 200.0f);
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

		// vrs settings.
		if (ImGui::CollapsingHeader("VRS", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Use VRS", &bUseVRS_);
			if (bUseVRS_)
			{
				ImGui::SliderFloat("Intensity Threshold", &vrsIntensityThreshold_, 0.0001f, 0.1f);
				ImGui::SliderFloat("Depth Threshold", &vrsDepthThreshold_, 0.1f, 100.0f);
			}
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
				"Indirect Light",
				"VRS",
			};
			ImGui::Combo("Display Mode", &displayMode_, kDisplayModes, ARRAYSIZE(kDisplayModes));

			ImGui::Checkbox("Texture Streaming", &bIsTexStreaming_);
			if (ImGui::Button("Miplevel Print"))
			{
				for (auto&& mat : scene_->GetMeshletResource()->GetWorldMaterials())
				{
					if (!mat.pResMaterial->baseColorTex.IsValid())
						continue;

					auto TexBase = mat.pResMaterial->baseColorTex.GetItem<sl12::ResourceItemTextureBase>();
					if (TexBase->IsSameSubType(sl12::ResourceItemStreamingTexture::kSubType))
					{
						auto Tex = mat.pResMaterial->baseColorTex.GetItem<sl12::ResourceItemStreamingTexture>();
						sl12::ConsolePrint("TexMiplevel: %s (%d)\n", Tex->GetFilePath().c_str(), Tex->GetCurrMipLevel());
					}
				}
			}
			const std::string kCaptureFileName = "CaptureResult.wpix";
			if (ImGui::Button("PIX Capture"))
			{
				captureFileName_ = CreateTimestampedFilename(kCaptureFileName);
				device_.CaptureGPUonPIX(captureFileName_);
			}
			static const char* kPoolSizes[] = {
				"Infinite",
				"256MB",
				"512MB",
				"1024MB"
			};
			if (ImGui::Combo("Pool Size", &poolSizeSelect_, kPoolSizes, ARRAYSIZE(kPoolSizes)))
			{
				static const sl12::u64 kPoolLimits[] = {
					0,
					256 * 1024 * 1024,
					512 * 1024 * 1024,
					1024 * 1024 * 1024,
				}; 
				device_.GetTextureStreamAllocator()->SetPoolLimitSize(kPoolLimits[poolSizeSelect_]);
			}
			ImGui::Text("Heaps : %lld (MB)", device_.GetTextureStreamAllocator()->GetCurrentHeapSize() / 1024 / 1024);
		}

		// gpu performance.
		if (ImGui::CollapsingHeader("GPU Time", ImGuiTreeNodeFlags_DefaultOpen))
		{
			totalTimeSum_ += scene_->GetRenderGraph()->GetAllPassMicroSec();
			totalTimeSumCount_++;
			if (totalTimeSumCount_ >= 60)
			{
				totalTime_ = totalTimeSum_ / (float)totalTimeSumCount_;
				totalTimeSum_ = 0.0f;
				totalTimeSumCount_ = 0;
			}
			ImGui::Text("total   : %f (ms)", totalTime_ / 1000.0f);

			auto pPerfResult = scene_->GetRenderGraph()->GetPerformanceResult();
			if (ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen))
			{
				size_t c = pPerfResult[sl12::HardwareQueue::Graphics].passNames.size();
				for (size_t i = 0; i < c; i++)
				{
					ImGui::Text("%s   : %f (ms)", pPerfResult[sl12::HardwareQueue::Graphics].passNames[i].c_str(), pPerfResult[sl12::HardwareQueue::Graphics].passMicroSecTimes[i] / 1000.0f);
				}
			}
			if (ImGui::CollapsingHeader("Compute", ImGuiTreeNodeFlags_DefaultOpen))
			{
				size_t c = pPerfResult[sl12::HardwareQueue::Compute].passNames.size();
				for (size_t i = 0; i < c; i++)
				{
					ImGui::Text("%s   : %f (ms)", pPerfResult[sl12::HardwareQueue::Compute].passNames[i].c_str(), pPerfResult[sl12::HardwareQueue::Compute].passMicroSecTimes[i] / 1000.0f);
				}
			}
		}
	}
	ImGui::Render();

	bool bNeedDeinterleave = bIsDeinterleave_ && (ssaoType_ == 2);

	// sync interval.
	device_.WaitPresent();
	device_.GetTextureStreamAllocator()->GabageCollect(renderSys_->GetTextureStreamer());
	device_.SyncKillObjects();

	// compile render graph.
	RenderPassSetupDesc setupDesc{};
	setupDesc.bUseVisibilityBuffer = bEnableVisibilityBuffer_;
	setupDesc.bUseMeshShader = bEnableMeshShader_;
	setupDesc.visToGBufferType = VisToGBufferType_;
	setupDesc.ssaoType = ssaoType_;
	setupDesc.bNeedDeinterleave = bIsDeinterleave_;
	setupDesc.bUseVRS = bUseVRS_;
	setupDesc.vrsIntensityThreshold = vrsIntensityThreshold_;
	setupDesc.vrsDepthThreshold = vrsDepthThreshold_;
	setupDesc.debugMode = displayMode_;
	scene_->SetupRenderPass(pSwapchainTarget, setupDesc);

	auto meshMan = renderSys_->GetMeshManager();
	auto cbvMan = renderSys_->GetCbvManager();

	pTimestamp->Reset();
	pTimestamp->Query(pFrameStartCmdList);
	device_.LoadRenderCommands(pFrameStartCmdList);
	meshMan->BeginNewFrame(pFrameStartCmdList);
	cbvMan->BeginNewFrame();

	// create scene constant buffer.
	auto&& TempCB = scene_->GetTemporalCBs();
	SetupConstantBuffers(TempCB);

	// create mesh cbuffers.
	for (auto&& mesh : scene_->GetSceneMeshes())
	{
		// set mesh constant.
		MeshCB cbMesh;
		cbMesh.mtxBoxTransform = mesh->GetParentResource()->GetMtxBoxToLocal();
		cbMesh.mtxLocalToWorld = mesh->GetMtxLocalToWorld();
		cbMesh.mtxPrevLocalToWorld = mesh->GetMtxPrevLocalToWorld();
		TempCB.hMeshCBs.push_back(cbvMan->GetTemporal(&cbMesh, sizeof(cbMesh)));
	}

	// create irradiance map.
	scene_->CreateIrradianceMap(pFrameStartCmdList);

	frameStartCmdlist_->Close();

	// load render graph command.
	scene_->LoadRenderGraphCommand();

	// draw GUI.
	pTimestamp->Query(pFrameEndCmdList);
	{
		// set render targets.
		auto&& rtv = device_.GetSwapchain().GetCurrentRenderTargetView(kSwapchainBufferOffset)->GetDescInfo().cpuHandle;
		pFrameEndCmdList->GetLatestCommandList()->OMSetRenderTargets(1, &rtv, false, nullptr);

		// set viewport.
		D3D12_VIEWPORT vp;
		vp.TopLeftX = vp.TopLeftY = 0.0f;
		vp.Width = (float)displayWidth_;
		vp.Height = (float)displayHeight_;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		pFrameEndCmdList->GetLatestCommandList()->RSSetViewports(1, &vp);

		// set scissor rect.
		D3D12_RECT rect;
		rect.left = rect.top = 0;
		rect.right = displayWidth_;
		rect.bottom = displayHeight_;
		pFrameEndCmdList->GetLatestCommandList()->RSSetScissorRects(1, &rect);

		gui_->LoadDrawCommands(pFrameEndCmdList);
	}

	// miplevel readback.
	if (bIsTexStreaming_)
	{
		auto miplevelBuffer = scene_->GetMiplevelBuffer();
		auto miplevelReadbacks = scene_->GetMiplevelReadbacks();
		auto&& materials = scene_->GetMeshletResource()->GetWorldMaterials();

		// process copied buffer.
		std::vector<sl12::u32> mipResults;
		if (miplevelReadbacks[1].IsValid())
		{
			mipResults.resize(miplevelReadbacks[0]->GetBufferDesc().size / sizeof(sl12::u32));
			void* p = miplevelReadbacks[0]->Map();
			memcpy(mipResults.data(), p, miplevelReadbacks[0]->GetBufferDesc().size);
			miplevelReadbacks[0]->Unmap();
			miplevelReadbacks[0] = std::move(miplevelReadbacks[1]);
		}

		ManageTextureStream(mipResults);

		// readback miplevel.
		UniqueHandle<sl12::Buffer> readback = sl12::MakeUnique<sl12::Buffer>(&device_);
		sl12::BufferDesc desc{};
		desc.heap = sl12::BufferHeap::ReadBack;
		desc.size = sizeof(sl12::u32) * materials.size();
		desc.usage = sl12::ResourceUsage::Unknown;
		readback->Initialize(&device_, desc);
		pFrameEndCmdList->TransitionBarrier(miplevelBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		pFrameEndCmdList->GetLatestCommandList()->CopyResource(readback->GetResourceDep(), miplevelBuffer->GetResourceDep());
		if (!miplevelReadbacks[0].IsValid())
		{
			miplevelReadbacks[0] = std::move(readback);
		}
		else
		{
			miplevelReadbacks[1] = std::move(readback);
		}

	}

	// barrier swapchain.
	pFrameEndCmdList->TransitionBarrier(device_.GetSwapchain().GetCurrentTexture(kSwapchainBufferOffset), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	// graphics timestamp end.
	pTimestamp->Query(pFrameEndCmdList);
	pTimestamp->Resolve(pFrameEndCmdList);
	frameEndCmdList_->Close();
	timestampIndex_ = 1 - timestampIndex_;

	// wait prev frame render.
	device_.WaitDrawDone();

	// present swapchain.
	device_.Present(1);

	// execute queue commands.
	device_.ExecuteQueueCommands();

	// execute current frame render.
	frameStartCmdlist_->Execute();
	scene_->ExecuteRenderGraphCommand();
	frameEndCmdList_->Execute();

	frameIndex_++;

	TempCB.Clear();

	return true;
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

void SampleApplication::ManageTextureStream(const std::vector<sl12::u32>& miplevels)
{
	int index = 0;

	auto&& neededMiplevels = scene_->GetNeededMiplevels();
	auto&& materials = scene_->GetMeshletResource()->GetWorldMaterials();

	// process needed levels.
	if (!miplevels.empty())
	{
		auto p = miplevels.data();
		for (auto&& s : neededMiplevels)
		{
			auto currLevel = materials[index].GetCurrentMiplevel();

			s.latestLevel = std::min(*p++, 0xffu);
			sl12::u32 minL = std::min(s.latestLevel, s.minLevel);

			s.minLevel = minL;
			if (currLevel == s.minLevel)
			{
				s.minLevel = 0xff;
				s.latestLevel = 0xff;
				s.time = 0;
			}
			else
			{
				s.time++;
			}

			index++;
		}
	}

	// request texture streaming.
	int i = 0;
	for (auto&& s : neededMiplevels)
	{
		bool bLatestCheck = (s.minLevel > s.latestLevel ? s.minLevel - s.latestLevel : s.latestLevel - s.minLevel) <= 1;
		if (!bLatestCheck)
		{
			s.minLevel = 0xff;
			s.latestLevel = 0xff;
			s.time = 0;
		}

		// if 30 frames elapsed.
		if (s.time >= 30)
		{
			sl12::u32 targetWidth = std::max(4096u >> s.minLevel, 1u);
			for (auto handle : materials[i].texHandles)
			{
				renderSys_->GetTextureStreamer()->RequestStreaming(handle, targetWidth);
			}
			s.minLevel = 0xff;
			s.latestLevel = 0xff;
			s.time = 0;
		}
		i++;
	}
}


//	EOF
