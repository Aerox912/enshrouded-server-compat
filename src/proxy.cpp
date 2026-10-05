#include "adapter.hpp"
#include <dbghelp.h>
#include <filesystem>

namespace {
HMODULE self;
std::wstring directory;
void log_line(const char* message) {
    const auto path = directory + L"\\xhl-server-adapter.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME time;
    GetLocalTime(&time);
    char prefix[64];
    int length = sprintf_s(prefix, "[%04u-%02u-%02u %02u:%02u:%02u] ", time.wYear, time.wMonth,
                           time.wDay, time.wHour, time.wMinute, time.wSecond);
    DWORD written;
    WriteFile(file, prefix, static_cast<DWORD>(length), &written, nullptr);
    WriteFile(file, message, static_cast<DWORD>(strlen(message)), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}
DWORD WINAPI initialize(void*) {
    directory = std::filesystem::path(xhl::module_path(self)).parent_path().wstring();
    HMODULE pinned;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       reinterpret_cast<LPCWSTR>(self), &pinned);
    log_line("XHL dedicated-server adapter 0.1.0 experimental starting.");
    const auto xp_path = directory + L"\\GlobalXPShare.original.dll";
    if (std::filesystem::exists(xp_path)) {
        if (!xhl::verify_file(xp_path, xhl::xp_hash)) {
            log_line("NOT APPLIED: existing XP Share DLL differs from the validated copy."); return 0;
        }
        if (!LoadLibraryExW(xp_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {
            log_line("NOT APPLIED: existing XP Share DLL could not load."); return 0;
        }
        log_line("Global XP Share DLL loaded; check its own log for HOOK INSTALLED.");
    } else {
        log_line("No GlobalXPShare.original.dll present; starting without XP Share.");
    }
    xhl::start(GetModuleHandleW(nullptr), directory + L"\\mods\\XHL-Vein-Mining\\XHL-Vein-Mining.dll", log_line);
    xhl::start_extras(GetModuleHandleW(nullptr), directory, log_line);
    return 0;
}
}

extern "C" BOOL WINAPI ProxyMiniDumpWriteDump(HANDLE process, DWORD pid, HANDLE file, MINIDUMP_TYPE type,
    PMINIDUMP_EXCEPTION_INFORMATION exception, PMINIDUMP_USER_STREAM_INFORMATION streams,
    PMINIDUMP_CALLBACK_INFORMATION callback) {
    using Function = BOOL (WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                    PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    static Function original = [] {
        wchar_t system[MAX_PATH];
        if (!GetSystemDirectoryW(system, MAX_PATH)) return static_cast<Function>(nullptr);
        std::wstring path = std::wstring(system) + L"\\dbghelp.dll";
        HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return module ? reinterpret_cast<Function>(GetProcAddress(module, "MiniDumpWriteDump")) : nullptr;
    }();
    if (!original) { SetLastError(ERROR_PROC_NOT_FOUND); return FALSE; }
    return original(process, pid, file, type, exception, streams, callback);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        DisableThreadLibraryCalls(module);
        // Loader-lock serialization prevents the worker from loading plugins
        // until this entry point returns. Never wait for it from DllMain.
        HANDLE thread = CreateThread(nullptr, 0, initialize, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
