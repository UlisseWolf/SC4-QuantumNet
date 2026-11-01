////////////////////////////////////////////////////////////////////////
//
// SC4-QuantumNet - minimal file logger (SC4QuantumNet.log next to the DLL).
//
////////////////////////////////////////////////////////////////////////

#pragma once
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

enum class LogLevel : int { Error = 0, Info = 1, Debug = 2 };

class Logger
{
public:
	static Logger& Get();

	void Init(const std::filesystem::path& path, LogLevel level);
	void SetLevel(LogLevel level);
	LogLevel ParseLevel(const std::string& text) const;

	void Write(LogLevel level, const std::string& message);
	void WriteFormatted(LogLevel level, const char* format, ...);

private:
	Logger() = default;

	std::ofstream file;
	LogLevel level = LogLevel::Info;
	std::mutex mutex;
};
