////////////////////////////////////////////////////////////////////////
//
// SC4-QuantumNet - configuration loader.
//
// Replaces the QuantumNet section of PACK_CONSTANTS.lua with a plain INI
// file (SC4QuantumNet.ini) placed next to the DLL. Every value has a
// compiled-in default identical to the former Lua constant, so the INI is
// optional and only needs the keys being overridden.
//
////////////////////////////////////////////////////////////////////////

#pragma once
#include "QuantumNetSimulation.h"
#include <filesystem>
#include <string>
#include <vector>

namespace QuantumNet
{
	struct ConfigLoadResult
	{
		bool fileFound = false;
		std::vector<std::string> warnings;
	};

	// Parses INI text on top of the given config (pure, testable).
	void ApplyIni(const std::string& text, Config& cfg, ConfigLoadResult& result);

	// Reads the INI file (if present) on top of Config::Defaults().
	Config LoadConfig(const std::filesystem::path& path, ConfigLoadResult& result);

	bool ParseUInt32(const std::string& text, uint32_t& out);
}
