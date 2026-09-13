// dllmain.cpp
#include "globals.hpp"
#include "ryze_paths.hpp"
#include <filesystem>
#include <iostream>
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>

// Global gate — true when build is allowed to run (external linkage so other translation units can see it).
std::atomic<bool> g_enabled{ true };

inline static void create_ryze_folder()
{
    // OLD CODE (kept for revert):
    // std::string path = "C:/Program Files (x86)/Steam/steamapps/common/Counter-Strike Global Offensive/airflow";
    //
    // All data lives in Documents\Ryze now; migrate the old airflow folder once.
    try {
        if (ryze_paths::ensure_folders())
            std::cout << "[+] Ryze folder: " << ryze_paths::root() << std::endl;

        ryze_paths::migrate_old_data();
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "[-] Failed to create Ryze folder: " << e.what() << std::endl;
    }
}

// ---------------------------------------------------------------------------
// Local crash reporter (no network). Writes Documents\Ryze\logs\crash_<time>.txt
// with the exception, address, module, registers, and the tail of the newest
// rage log, so a crash can be diagnosed from the files alone.
// ---------------------------------------------------------------------------
static HMODULE g_cheat_module{};
static volatile LONG g_crashing = 0;

static void append_newest_rage_log(HANDLE out)
{
	char docs[MAX_PATH]{};
	if (FAILED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs)))
		return;

	char pattern[MAX_PATH * 2]{};
	wsprintfA(pattern, "%s\\Ryze\\logs\\ryze_*.log", docs);

	WIN32_FIND_DATAA fd{};
	HANDLE find = FindFirstFileA(pattern, &fd);
	if (find == INVALID_HANDLE_VALUE)
		return;

	char newest[MAX_PATH * 2]{};
	FILETIME best{};
	bool have = false;
	do
	{
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			&& (!have || CompareFileTime(&fd.ftLastWriteTime, &best) > 0))
		{
			best = fd.ftLastWriteTime;
			wsprintfA(newest, "%s\\Ryze\\logs\\%s", docs, fd.cFileName);
			have = true;
		}
	} while (FindNextFileA(find, &fd));
	FindClose(find);

	if (!have)
		return;

	HANDLE log = CreateFileA(newest, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (log == INVALID_HANDLE_VALUE)
		return;

	LARGE_INTEGER size{};
	GetFileSizeEx(log, &size);
	LARGE_INTEGER pos{};
	pos.QuadPart = size.QuadPart > 4096 ? size.QuadPart - 4096 : 0;
	SetFilePointerEx(log, pos, nullptr, FILE_BEGIN);

	char tail[4097]{};
	DWORD read = 0;
	if (ReadFile(log, tail, 4096, &read, nullptr) && read > 0)
	{
		DWORD w = 0;
		const char head[] = "\r\n--- tail of ";
		WriteFile(out, head, (DWORD)(sizeof(head) - 1), &w, nullptr);
		WriteFile(out, newest, (DWORD)lstrlenA(newest), &w, nullptr);
		WriteFile(out, " ---\r\n", 6, &w, nullptr);
		WriteFile(out, tail, read, &w, nullptr);
	}

	CloseHandle(log);
}

// Resolve an address to its owning image + module-relative offset. Returns the
// module base and fills `name`/`offset`; if the address is not inside an image
// (heap/stack/manual map) the allocation base is returned with an empty name.
static HMODULE resolve_module(DWORD address, char* name, size_t name_count, DWORD& offset)
{
	offset = 0;
	if (name && name_count)
		name[0] = '\0';
	if (!address)
		return nullptr;

	HMODULE module = nullptr;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCSTR>(address), &module) && module)
	{
		offset = address - static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(module));
		if (name && name_count)
			GetModuleFileNameA(module, name, static_cast<DWORD>(name_count));
		return module;
	}

	MEMORY_BASIC_INFORMATION mbi{};
	if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)))
	{
		module = static_cast<HMODULE>(mbi.AllocationBase);
		offset = address - static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(mbi.AllocationBase));
	}

	return module;
}

static void append_game_context(HANDLE out)
{
	__try
	{
		if (!HACKS || !HACKS->cmd)
			return;

		char line[256]{};
		wsprintfA(line, "cmd=%d tick=%d choked=%d\r\n",
			HACKS->cmd->command_number,
			HACKS->global_vars ? HACKS->global_vars->tickcount : 0,
			HACKS->client_state ? HACKS->client_state->choked_commands : 0);

		DWORD written = 0;
		WriteFile(out, line, (DWORD)lstrlenA(line), &written, nullptr);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

static void write_crash_report(EXCEPTION_POINTERS* ex)
{
	char docs[MAX_PATH]{};
	if (FAILED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs)))
		return;

	char dir[MAX_PATH * 2]{};
	wsprintfA(dir, "%s\\Ryze", docs);
	CreateDirectoryA(dir, nullptr);
	wsprintfA(dir, "%s\\Ryze\\logs", docs);
	CreateDirectoryA(dir, nullptr);

	SYSTEMTIME st{};
	GetLocalTime(&st);

	char file[MAX_PATH * 2]{};
	wsprintfA(file, "%s\\Ryze\\logs\\crash_%04d-%02d-%02d_%02d-%02d-%02d.txt",
		docs, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	HANDLE out = CreateFileA(file, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (out == INVALID_HANDLE_VALUE)
		return;

	char module_name[MAX_PATH]{};
	if (g_cheat_module)
		GetModuleFileNameA(g_cheat_module, module_name, MAX_PATH);

	const auto* rec = ex ? ex->ExceptionRecord : nullptr;
	const auto* ctx = ex ? ex->ContextRecord : nullptr;

	char buf[1200]{};
	wsprintfA(buf,
		"Ryze crash report\r\n"
		"time: %04d-%02d-%02d %02d:%02d:%02d\r\n"
		"exception: 0x%08X\r\n"
		"address: 0x%08X\r\n"
		"cheat module: %s\r\n"
		"eip 0x%08X  esp 0x%08X  ebp 0x%08X\r\n"
		"eax 0x%08X  ebx 0x%08X  ecx 0x%08X  edx 0x%08X\r\n"
		"esi 0x%08X  edi 0x%08X\r\n",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
		rec ? rec->ExceptionCode : 0,
		rec ? (DWORD)rec->ExceptionAddress : 0,
		module_name,
		ctx ? ctx->Eip : 0, ctx ? ctx->Esp : 0, ctx ? ctx->Ebp : 0,
		ctx ? ctx->Eax : 0, ctx ? ctx->Ebx : 0, ctx ? ctx->Ecx : 0, ctx ? ctx->Edx : 0,
		ctx ? ctx->Esi : 0, ctx ? ctx->Edi : 0);

	DWORD written = 0;
	WriteFile(out, buf, (DWORD)lstrlenA(buf), &written, nullptr);

	// Which module owns the fault, and where the cheat itself is mapped. The
	// module-relative offset can be resolved against the matching PDB later.
	char fault_module[MAX_PATH]{};
	DWORD fault_offset = 0;
	HMODULE fault_base = resolve_module(
		static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(rec ? rec->ExceptionAddress : nullptr)),
		fault_module, sizeof(fault_module), fault_offset);

	char header[1200]{};
	wsprintfA(header,
		"cheat base: 0x%08X\r\n"
		"fault module: %s (base 0x%08X) + 0x%X\r\n"
		"stack:\r\n",
		static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(g_cheat_module)),
		fault_module[0] ? fault_module : "(not in an image)",
		static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(fault_base)),
		fault_offset);
	WriteFile(out, header, (DWORD)lstrlenA(header), &written, nullptr);

	using rtl_capture_t = USHORT(NTAPI*)(ULONG, ULONG, PVOID*, PULONG);
	HMODULE ntdll = GetModuleHandleA("ntdll.dll");
	auto capture = ntdll
		? reinterpret_cast<rtl_capture_t>(GetProcAddress(ntdll, "RtlCaptureStackBackTrace"))
		: nullptr;

	void* frames[24]{};
	USHORT frame_count = capture ? capture(1, 24, frames, nullptr) : 0;
	for (USHORT i = 0; i < frame_count; ++i)
	{
		char frame_module[MAX_PATH]{};
		DWORD frame_offset = 0;
		resolve_module(static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(frames[i])),
			frame_module, sizeof(frame_module), frame_offset);

		char line[512]{};
		wsprintfA(line, "  [%02u] %s + 0x%X\r\n",
			i, frame_module[0] ? frame_module : "(private)", frame_offset);
		WriteFile(out, line, (DWORD)lstrlenA(line), &written, nullptr);
	}

	append_game_context(out);

	append_newest_rage_log(out);

	CloseHandle(out);
}

static LONG __stdcall crash_exception_handler(EXCEPTION_POINTERS* ex)
{
	// Only log fatal exceptions (access violations etc.), not handled
	// first-chance exceptions.
	if (!ex || !ex->ExceptionRecord || ex->ExceptionRecord->ExceptionCode < 0x80000000)
		return EXCEPTION_CONTINUE_SEARCH;

	if (InterlockedExchange(&g_crashing, 1) != 0)
		return EXCEPTION_CONTINUE_SEARCH;

	__try
	{
		write_crash_report(ex);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}

	InterlockedExchange(&g_crashing, 0);
	return EXCEPTION_CONTINUE_SEARCH;
}

// Note: ensure hacks.hpp contains `#include <atomic>` and `extern std::atomic<bool> g_enabled;`
// so init_cheat can read the flag. init_cheat should call `if (!g_enabled.load()) return;` at start.

// Forward declaration for your initialization routine.
// Adjust signature if your init_cheat uses different params.
extern void init_cheat(void* reserved);

// DllMain
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        // Avoid thread attach/detach notifications to reduce work on each thread creation.
        DisableThreadLibraryCalls(module);

        // Local crash catcher (writes Documents\Ryze\logs\crash_<time>.txt).
        g_cheat_module = module;
        AddVectoredExceptionHandler(1, crash_exception_handler);

        // Create/migrate the Ryze data folder (Documents\Ryze)
        create_ryze_folder();

#ifdef _DEBUG
        // If you must use CreateThread in debug, do it carefully. Prefer starting work via a detached std::thread.
        // If HACKS is available and has modules struct, set dllmain pointer.
#ifdef HACKS
        HACKS->modules.dllmain = module;
#endif

        // launch init_cheat on a detached std::thread
        {
            std::thread([](LPVOID r) {
                init_cheat(r);
                }, reserved).detach();
        }
#else
        // launch initialization on a detached std::thread.
        // Use a lambda wrapper so we don't call system APIs that are unsafe from DllMain.
        {
            std::thread([reserved]() {
                // init_cheat should check g_enabled at its entry point and exit early if false.
                init_cheat(reserved);
                }).detach();
        }
#endif
        return TRUE;
    }

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        // we disabled thread notifications, so these won't be called
        break;

    case DLL_PROCESS_DETACH:
        // optional cleanup; do not perform complex actions here
        break;
    }

    return TRUE;
}
