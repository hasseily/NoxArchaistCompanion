#include "pch.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include "NonVolatile.h"
#include "nlohmann/json.hpp"

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
	std::ofstream out(configfilename);
	out << std::setw(4) << nv_json << std::endl;
	out.close();
    return 0;
}

int NonVolatile::LoadFromDisk()
{
	nlohmann::json j;
	std::ifstream in(configfilename);
	in >> j;
	in.close();
	nv_json.merge_patch(j);
	profilePath			= nv_json["profilePath"].get<std::wstring>();
	hdvPath				= nv_json["hdvPath"].get<std::wstring>();
	volumeSpeaker		= nv_json["volumeSpeaker"].get<int>();
	volumeMockingBoard	= nv_json["volumeMockingBoard"].get<int>();
	useGameLink			= nv_json["useGameLink"].get<bool>();
	return 0;
}
