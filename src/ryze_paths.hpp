#pragma once
#include <string>
#include <filesystem>
#include <system_error>
#include <windows.h>
#include <ShlObj.h>
#pragma comment(lib, "Shell32.lib")

// Every persistent cheat file lives in Documents\Ryze now (configs, sounds,
// trash talk, rage debug log). On first run the old CS:GO "airflow" folder
// contents are moved over automatically.
namespace ryze_paths
{
	inline std::string documents()
	{
		char path[MAX_PATH]{};
		if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, nullptr, 0, path)))
			return std::string(path);

		return {};
	}

	inline std::string root()
	{
		const std::string docs = documents();
		if (docs.empty())
			return "Ryze"; // fallback: relative folder

		return docs + "\\Ryze";
	}

	inline std::string config_folder() { return root() + "\\"; }
	inline std::string sounds_folder() { return root() + "\\sounds"; }
	inline std::string trash_talk_folder() { return root() + "\\Trash Talk"; }
	// Logs live in their own folder and are created there only when enabled.
	inline std::string logs_folder() { return root() + "\\logs"; }
	inline std::string log_file() { return root() + "\\ryze_rage_debug.log"; }

	inline bool ensure_folders()
	{
		std::error_code ec;
		std::filesystem::create_directories(root(), ec);
		std::filesystem::create_directories(sounds_folder(), ec);
		std::filesystem::create_directories(trash_talk_folder(), ec);
		return std::filesystem::exists(root(), ec);
	}

	// Move old data (CS:GO airflow) into Documents\Ryze exactly once. Existing
	// destination files are never overwritten.
	inline void migrate_old_data()
	{
		std::error_code ec;
		const std::filesystem::path target = root();
		if (!std::filesystem::is_directory(target, ec) || ec)
			return;

		const char* old_folders[] = {
			"airflow",
			"C:/Program Files (x86)/Steam/steamapps/common/Counter-Strike Global Offensive/airflow",
		};

		for (const char* old_folder : old_folders)
		{
			std::filesystem::path source = old_folder;
			ec.clear();
			if (!std::filesystem::is_directory(source, ec) || ec)
				continue;

			for (const auto& entry : std::filesystem::directory_iterator(source, ec))
			{
				if (ec)
					break;

				const auto destination = target / entry.path().filename();
				if (std::filesystem::exists(destination, ec))
					continue;

				std::filesystem::rename(entry.path(), destination, ec);
				if (ec)
				{
					ec.clear();
					std::filesystem::copy(entry.path(), destination,
						std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
					if (!ec)
						std::filesystem::remove_all(entry.path(), ec);
				}
			}
		}
	}
}
