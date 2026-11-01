////////////////////////////////////////////////////////////////////////
//
// SC4-QuantumNet - minimal file logger.
//
////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <vector>

Logger& Logger::Get()
{
	static Logger instance;
	return instance;
}

void Logger::Init(const std::filesystem::path& path, LogLevel lvl)
{
	std::lock_guard<std::mutex> lock(mutex);
	level = lvl;
	file.open(path, std::ios::out | std::ios::trunc);
}

void Logger::SetLevel(LogLevel lvl)
{
	std::lock_guard<std::mutex> lock(mutex);
	level = lvl;
}

LogLevel Logger::ParseLevel(const std::string& text) const
{
	std::string t;
	for (char c : text) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
	if (t == "error") return LogLevel::Error;
	if (t == "debug") return LogLevel::Debug;
	return LogLevel::Info;
}

void Logger::Write(LogLevel lvl, const std::string& message)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (!file.is_open() || static_cast<int>(lvl) > static_cast<int>(level))
	{
		return;
	}

	char stamp[32] = {};
	const std::time_t now = std::time(nullptr);
	std::tm tmNow{};
#ifdef _WIN32
	localtime_s(&tmNow, &now);
#else
	localtime_r(&now, &tmNow);
#endif
	std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmNow);

	static const char* names[] = { "ERROR", "INFO ", "DEBUG" };
	file << stamp << " [" << names[static_cast<int>(lvl)] << "] " << message << '\n';
	file.flush();
}

void Logger::WriteFormatted(LogLevel lvl, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	va_list copy;
	va_copy(copy, args);
	const int len = std::vsnprintf(nullptr, 0, format, copy);
	va_end(copy);

	if (len > 0)
	{
		std::vector<char> buffer(static_cast<size_t>(len) + 1);
		std::vsnprintf(buffer.data(), buffer.size(), format, args);
		Write(lvl, std::string(buffer.data(), static_cast<size_t>(len)));
	}
	va_end(args);
}
