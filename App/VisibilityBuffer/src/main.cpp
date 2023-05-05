#include "sample_application.h"
#include "sl12/string_util.h"


namespace
{
#if 0
	static const int	kDisplayWidth  = 2560;
	static const int	kDisplayHeight = 1440;
#else
	static const int	kDisplayWidth  = 1280;
	static const int	kDisplayHeight = 720;
#endif
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	auto ColorSpace = sl12::ColorSpaceType::Rec709;
	std::string homeDir = ".\\";
	int meshType = 0;

	LPWSTR *szArglist;
	int nArgs;

	szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
	if (szArglist)
	{
		for (int i = 0; i < nArgs; i++)
		{
			if (!lstrcmpW(szArglist[i], L"-hdr"))
			{
				ColorSpace = sl12::ColorSpaceType::Rec2020;
			}
			else if (!lstrcmpW(szArglist[i], L"-homedir"))
			{
				homeDir = sl12::WStringToString(szArglist[++i]);
			}
			else if (!lstrcmpW(szArglist[i], L"-mesh"))
			{
				meshType = std::stoi(szArglist[++i]);
			}
		}
	}

	SampleApplication app(hInstance, nCmdShow, kDisplayWidth, kDisplayHeight, ColorSpace, homeDir, meshType);

	return app.Run();
}

//	EOF
