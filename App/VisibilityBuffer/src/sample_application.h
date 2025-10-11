#include "scene.h"

#include "sl12/application.h"
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

#include <memory>
#include <queue>
#include <vector>

#include "sl12/resource_streaming_texture.h"
#include "sl12/scene_mesh.h"
#include "sl12/texture_streamer.h"
#include "sl12/timestamp.h"
#include "sl12/work_graph.h"


class SampleApplication
	: public sl12::Application
{
	template <typename T> using UniqueHandle = sl12::UniqueHandle<T>;
	
	struct NeededMiplevel
	{
		sl12::u32	minLevel;
		sl12::u32	latestLevel;
		int			time;
	};	// struct NeededMiplevel

public:
	SampleApplication(HINSTANCE hInstance, int nCmdShow, int screenWidth, int screenHeight, sl12::ColorSpaceType csType, const std::string& homeDir, int meshType, const std::string& appShader, const std::string& sysShader);
	virtual ~SampleApplication();

	// virtual
	virtual bool Initialize() override;
	virtual bool Execute() override;
	virtual void Finalize() override;
	virtual int Input(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
	void ControlCamera(float deltaTime = 1.0f / 60.0f);

	void SetupConstantBuffers(struct TemporalCBs& OutCBs);

	void ManageTextureStream(const std::vector<sl12::u32>& miplevels);

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
	std::string		appShaderDir_, sysShaderInclDir_;

	UniqueHandle<RenderSystem>	renderSys_;
	UniqueHandle<Scene>			scene_;
	UniqueHandle<CommandLists>	mainCmdList_;
	UniqueHandle<CommandLists>	frameStartCmdlist_;
	UniqueHandle<CommandLists>	frameEndCmdList_;
	UniqueHandle<sl12::RenderGraph_Deprecated>		renderGraphDeprecated_;

	UniqueHandle<sl12::Texture>				detailDerivTex_;
	UniqueHandle<sl12::TextureView>			detailDerivSrv_;

	UniqueHandle<sl12::Buffer>				indirectArgBuffer_;
	UniqueHandle<sl12::Buffer>				indirectArgUpload_;
	UniqueHandle<sl12::UnorderedAccessView>	indirectArgUAV_;
	UniqueHandle<sl12::IndirectExecuter>	meshletIndirectStandard_, meshletIndirectVisbuffer_;

	UniqueHandle<sl12::Gui>		gui_;
	sl12::InputData				inputData_{};

	// history.
	DirectX::XMMATRIX		mtxPrevWorldToView_, mtxPrevViewToClip_, mtxPrevWorldToClip_;

	sl12::Timestamp			timestamps_[2];
	sl12::u32				timestampIndex_ = 0;
	sl12::CpuTimer			currCpuTime_;

	// camera parameters.
	DirectX::XMFLOAT3		cameraPos_;
	DirectX::XMFLOAT3		cameraDir_;
	int						lastMouseX_, lastMouseY_;

	// rendering parameters.
	bool					bEnableVisibilityBuffer_ = false;
	bool					bEnableMeshShader_ = false;
	int						VisToGBufferType_ = 0;
	bool					bEnableWorkGraph_ = false;
	
	// light parameters.
	float					ambientIntensity_ = 0.1f;
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

	// ssao parameters.
	int						ssaoType_ = 0;
	float					ssaoIntensity_ = 1.0f;
	float					ssgiIntensity_ = 1.0f;
	int						ssaoSliceCount_ = 8;
	int						ssaoStepCount_ = 8;
	int						ssaoMaxPixel_ = 32;
	float					ssaoWorldRadius_ = 30.0f;
	float					ssaoTangentBias_ = 0.3f;
	float					ssaoConstThickness_ = 1.0f;
	float					ssaoViewBias_ = 1.0f;
	int						ssaoBaseVecType_ = 0;
	bool					bIsDeinterleave_ = false;
	float					denoiseRadius_ = 2.0f;
	float					denoiseBaseWeight_ = 0.85f;
	float					denoiseDepthSigma_ = 1.0f;

	// vrs parameters.
	bool					bUseVRS_ = false;
	float					vrsIntensityThreshold_ = 0.02f;
	float					vrsDepthThreshold_ = 1.0f;

	// debug parameters.
	int						displayMode_ = 0;
	bool					bIsTexStreaming_ = true;
	int						poolSizeSelect_ = 0;
	float					totalTime_ = 0;
	float					totalTimeSum_ = 0;
	int						totalTimeSumCount_ = 0;
	
	int	displayWidth_, displayHeight_;
	int meshType_;
	sl12::u64	frameIndex_ = 0;

	std::string				captureFileName_;
};	// class SampleApplication

//	EOF
