#include "globals.hpp"
#include "rage_logger.hpp"
#include "ryze_paths.hpp"

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <windows.h>

namespace rage_log
{
	static std::mutex g_mutex;
	static std::ofstream g_file;
	static std::string g_path;
	static bool g_open_attempted = false;
	static bool g_header_written = false;

	// OLD CODE (kept for revert):
	// static const char* kAirflowDir = "C:/Program Files (x86)/Steam/steamapps/common/Counter-Strike Global Offensive/airflow";
	//
	// Logs now go to Documents\Ryze\logs with the creation date + time in the
	// filename. The logs folder is only created when logging is first used.

	static void open_locked()
	{
		if (g_open_attempted)
			return;

		g_open_attempted = true;

		// Fixed per session: creation date + time.
		std::time_t now = std::time(nullptr);
		std::tm tm_buf{};
		localtime_s(&tm_buf, &now);
		char stamp[32]{};
		std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm_buf);

		const std::string name = std::string("ryze_") + stamp + ".log";

		std::error_code ec;
		const std::filesystem::path logs_dir = ryze_paths::logs_folder();
		std::filesystem::create_directories(logs_dir, ec);

		std::filesystem::path target = logs_dir / name;
		g_file.open(target, std::ios::out | std::ios::app);

		if (!g_file.is_open())
		{
			char temp_path[MAX_PATH]{};
			GetTempPathA(sizeof(temp_path), temp_path);
			target = std::filesystem::path(temp_path) / name;
			g_file.open(target, std::ios::out | std::ios::app);
		}

		if (g_file.is_open())
			g_path = target.string();
	}

	bool enabled()
	{
		return g_cfg.rage.debug_log;
	}

	const std::string& path()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		open_locked();
		return g_path;
	}

	void line(const char* fmt, ...)
	{
		if (!enabled())
			return;

		char body[2048]{};
		va_list args;
		va_start(args, fmt);
		vsnprintf(body, sizeof(body) - 1, fmt, args);
		va_end(args);

		std::lock_guard<std::mutex> lock(g_mutex);
		open_locked();
		if (!g_file.is_open())
			return;

		const int tick = HACKS && HACKS->global_vars ? HACKS->global_vars->tickcount : 0;
		const float curtime = HACKS && HACKS->global_vars ? HACKS->global_vars->curtime : 0.f;

		char prefix[64]{};
		snprintf(prefix, sizeof(prefix), "[%06d|%8.3f] ", tick, curtime);

		g_file << prefix << body << '\n';
		g_file.flush();
	}

	void session_header()
	{
		if (!enabled())
			return;

		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_header_written)
			return;

		open_locked();
		if (!g_file.is_open())
			return;

		g_header_written = true;

		const std::time_t now = std::time(nullptr);
		char time_str[64]{};
		std::tm tm_buf{};
		localtime_s(&tm_buf, &now);
		std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

		g_file << "==================== Ryze ragebot debug ====================\n";
		g_file << "session " << time_str << '\n';
		g_file << "log: " << g_path << '\n';
		g_file.flush();
	}
}
