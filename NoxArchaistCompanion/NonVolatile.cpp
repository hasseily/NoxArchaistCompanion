#include "pch.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include "NonVolatile.h"
#include "nlohmann/json.hpp"
#include "HAUtils.h"

static nlohmann::json nv_json = R"(
  {
    "profilePath": "Profiles\\Nox Archaist.json",
    "hdvPath": "NOXARCHAIST.HDV",
    "volumeSpeaker":	  20,
    "volumeMockingBoard": 20,
	"useGameLink": 0
  }
)"_json;

static std::string configfilename = "noxcompanion.conf";

int NonVolatile::SaveToDisk()
{
	std::string sprofPath;
	std::string sHdvPath;
	HA::ConvertWStrToStr(&profilePath, &sprofPath);
	HA::ConvertWStrToStr(&hdvPath, &sHdvPath);

	nv_json["profilePath"]			= sprofPath;
	nv_json["hdvPath"]				= sHdvPath;
	nv_json["volumeSpeaker"]		= volumeSpeaker;
	nv_json["volumeMockingBoard"]	= volumeMockingBoard;
	nv_json["useGameLink"]			= useGameLink;
	std::ofstream out(configfilename);
	out << std::setw(4) << nv_json << std::endl;
	out.close();
    return 0;
}

int NonVolatile::LoadFromDisk()
{
	nlohmann::json j;
	std::ifstream in(configfilename);
	if (!in.fail())
	{
		in >> j;
		in.close();
		nv_json.merge_patch(j);
	}

	std::string _profilePath = nv_json["profilePath"].get<std::string>();
	HA::ConvertStrToWStr(&_profilePath, &profilePath);
	std::string _hdvPath = nv_json["hdvPath"].get<std::string>();
	HA::ConvertStrToWStr(&_hdvPath, &hdvPath);

	volumeSpeaker = nv_json["volumeSpeaker"].get<int>();
	volumeMockingBoard = nv_json["volumeMockingBoard"].get<int>();
	useGameLink = nv_json["useGameLink"].get<bool>();

	return 0;
}
