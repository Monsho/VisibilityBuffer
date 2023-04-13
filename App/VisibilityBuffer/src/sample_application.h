#include "sl12/application.h"
#include "sl12/resource_loader.h"
#include "sl12/shader_manager.h"
#include "sl12/command_list.h"
#include "sl12/gui.h"
#include "sl12/root_signature.h"
#include "sl12/pipeline_state.h"
#include "sl12/unique_handle.h"
#include "sl12/cbv_manager.h"
#include "sl12/render_graph.h"
#include "sl12/indirect_executer.h"

#include <memory>
#include <vector>

#include "sl12/scene_mesh.h"
#include "sl12/timestamp.h"


class SampleApplication
	: public sl12::Application
{
	template <typename T> using UniqueHandle = sl12::UniqueHandle<T>;
	
public:
	SampleApplication(HINSTANCE hInstance, int nCmdShow, int screenWidth, int screenHeight, sl12::ColorSpaceType csType, const std::string& homeDir, int meshType);
	virtual ~SampleApplication();

	// virtual
	virtual bool Initialize() override;
	virtual bool Execute() override;
	virtual void Finalize() override;
	virtual int Input(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
	void CreateBuffers(sl12::CommandList* pCmdList, std::vector<sl12::ResourceItemMesh::Material>& outMaterials);

	void ControlCamera(float deltaTime = 1.0f / 60.0f);

	void ComputeSceneAABB();

private:
	static const int kBufferCount = sl12::Swapchain::kMaxBuffer;

	struct CommandLists
	{
		sl12::CommandList	cmdLists[kBufferCount];
		int					index = 0;

		CommandLists()
		{}
		~CommandLists()
		{
			Destroy();
		}

		bool Initialize(sl12::Device* pDev, sl12::CommandQueue* pQueue)
		{
			for (auto&& v : cmdLists)
			{
				if (!v.Initialize(pDev, pQueue, true))
				{
					return false;
				}
			}
			index = 0;
			return true;
		}

		void Destroy()
		{
			for (auto&& v : cmdLists) v.Destroy();
		}

		sl12::CommandList& Reset()
		{
			index = (index + 1) % kBufferCount;
			auto&& ret = cmdLists[index];
			ret.Reset();
			return ret;
		}

		void Close()
		{
			cmdLists[index].Close();
		}

		void Execute()
		{
			cmdLists[index].Execute();
		}

		sl12::CommandQueue* GetParentQueue()
		{
			return cmdLists[index].GetParentQueue();
		}
	};	// struct CommandLists

private:
	std::string		homeDir_;
	
	UniqueHandle<sl12::ResourceLoader>	resLoader_;
	UniqueHandle<sl12::ShaderManager>	shaderMan_;
	UniqueHandle<sl12::MeshManager>		meshMan_;
	UniqueHandle<CommandLists>			mainCmdList_;
	UniqueHandle<sl12::CbvManager>		cbvMan_;
	UniqueHandle<sl12::RenderGraph>		renderGraph_;

	// root sig & pso.
	UniqueHandle<sl12::RootSignature>			rsVsPs_;
	UniqueHandle<sl12::RootSignature>			rsCs_;
	UniqueHandle<sl12::GraphicsPipelineState>	psoMesh_;
	UniqueHandle<sl12::GraphicsPipelineState>	psoTriplanar_;
	UniqueHandle<sl12::GraphicsPipelineState>	psoVisibility_;
	UniqueHandle<sl12::GraphicsPipelineState>	psoMatDepth_;
	UniqueHandle<sl12::GraphicsPipelineState>	psoTonemap_;
	UniqueHandle<sl12::GraphicsPipelineState>	psoMaterialTile_;
	UniqueHandle<sl12::GraphicsPipelineState>	psoShadowDepth_;
	UniqueHandle<sl12::GraphicsPipelineState>	psoShadowExp_;
	UniqueHandle<sl12::GraphicsPipelineState>	psoBlur_;
	UniqueHandle<sl12::ComputePipelineState>	psoLighting_;
	UniqueHandle<sl12::ComputePipelineState>	psoClassify_;
	UniqueHandle<sl12::ComputePipelineState>	psoClearArg_;
	UniqueHandle<sl12::ComputePipelineState>	psoNormalToDeriv_;

	UniqueHandle<sl12::Sampler>				linearSampler_;
	UniqueHandle<sl12::Sampler>				linearClampSampler_;
	UniqueHandle<sl12::Sampler>				shadowSampler_;
	UniqueHandle<sl12::Sampler>				evsmSampler_;

	UniqueHandle<sl12::IndirectExecuter>	tileDrawIndirect_;
	
	UniqueHandle<sl12::Buffer>				instanceB_, submeshB_, drawCallB_;
	UniqueHandle<sl12::BufferView>			instanceBV_, submeshBV_, drawCallBV_;
	UniqueHandle<sl12::Buffer>				drawArgB_, tileIndexB_;
	UniqueHandle<sl12::BufferView>			tileIndexBV_;
	UniqueHandle<sl12::UnorderedAccessView>	drawArgUAV_, tileIndexUAV_;

	UniqueHandle<sl12::Texture>				detailDerivTex_;
	UniqueHandle<sl12::TextureView>			detailDerivSrv_;

	UniqueHandle<sl12::Gui>		gui_;
	sl12::InputData				inputData_{};

	// resources.
	sl12::ResourceHandle	hSuzanneMesh_;
	sl12::ResourceHandle	hSponzaMesh_;
	sl12::ResourceHandle	hSphereMesh_;
	sl12::ResourceHandle	hDetailTex_;
	sl12::ResourceHandle	hDotTex_;

	// shaders.
	std::vector<sl12::ShaderHandle>	hShaders_;

	std::vector<std::shared_ptr<sl12::SceneMesh>>	sceneMeshes_;
	DirectX::XMFLOAT3		sceneAABBMax_, sceneAABBMin_;

	sl12::Timestamp			timestamps_[2];
	sl12::u32				timestampIndex_ = 0;
	sl12::CpuTimer			currCpuTime_;

	// camera parameters.
	DirectX::XMFLOAT3		cameraPos_;
	DirectX::XMFLOAT3		cameraDir_;
	int						lastMouseX_, lastMouseY_;

	// light parameters.
	float					skyColor_[3] = {0.565f, 0.843f, 0.925f};
	float					groundColor_[3] = {0.639f, 0.408f, 0.251f};
	float					ambientIntensity_ = 0.3f;
	float					directionalTheta_ = 30.0f;
	float					directionalPhi_ = 45.0f;
	float					directionalColor_[3] = {1.0f, 1.0f, 1.0f};
	float					directionalIntensity_ = 3.0f;

	// shadow parameters.
	float					shadowBias_ = 0.001f;
	float					shadowExponent_ = 10.0f;
	bool					evsmBlur_ = true;

	// surface gradient parameters.
	float					detailTile_ = 3.0f;
	float					detailIntensity_ = 1.0f;
	int						detailType_ = 0;
	float					triplanarTile_ = 0.01f;
	int						triplanarType_ = 0;
	
	int	displayWidth_, displayHeight_;
	int meshType_;

	bool bEnableVisibilityBuffer_ = false;
};	// class SampleApplication

//	EOF
