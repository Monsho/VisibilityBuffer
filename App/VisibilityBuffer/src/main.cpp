#include "sample_application.h"
#include "sl12/string_util.h"
#include <regex>


namespace
{
	static const int	kDisplayWidth  = 2560;
	static const int	kDisplayHeight = 1440;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	auto ColorSpace = sl12::ColorSpaceType::Rec709;
	std::string homeDir = ".\\";
	std::string appShaderDir = "";
	std::string sysShaderInclDir = "";
	int meshType = 3;
	int screenWidth = kDisplayWidth;
	int screenHeight = kDisplayHeight;

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
			else if (!lstrcmpW(szArglist[i], L"-res"))
			{
				std::wstring str = szArglist[++i];
				std::wregex r(L"([0-9]+)x([0-9]+)");
				std::wsmatch m;
				if (std::regex_match(str, m, r))
				{
					screenWidth = std::stoi(m[1].str());
					screenHeight = std::stoi(m[2].str());
				}
			}
			else if (!lstrcmpW(szArglist[i], L"-appshader"))
			{
				appShaderDir = sl12::WStringToString(szArglist[++i]);
			}
			else if (!lstrcmpW(szArglist[i], L"-sysshader"))
			{
				sysShaderInclDir = sl12::WStringToString(szArglist[++i]);
			}
		}
	}

	SampleApplication app(hInstance, nCmdShow, screenWidth, screenHeight, ColorSpace, homeDir, meshType, appShaderDir, sysShaderInclDir);

	return app.Run();
}

//	EOF
