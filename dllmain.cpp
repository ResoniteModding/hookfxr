#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>

#define HOSTFXR_MAX_PATH 1024

namespace
{
wchar_t g_OriginHostfxrPath[HOSTFXR_MAX_PATH] = { '\0' };
wchar_t g_OriginDotnetPath[HOSTFXR_MAX_PATH] = { '\0' };
wchar_t g_ModifiedAppPath[HOSTFXR_MAX_PATH] = { '\0' };
HMODULE g_OriginHostfxrModule{ nullptr };

// src/native/corehost/error_codes.h
enum StatusCode
{
    FrameworkMissingFailure = 0x80008096,
};
    
void find_global_dotnet()
{
    // We don't need error handling in this function - if the registry key is not found, and we can't load the global
    // hostfxr, and our proxy funcs will return FrameworkMissingFailure which causes the apphost to show
    // an error message telling users to install dotnet.
    // TODO: Replace with code from nethost
    
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\dotnet\\Setup\\InstalledVersions\\x64\\sharedhost", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
        return;
    }

    DWORD path_size = sizeof(g_OriginHostfxrPath);
    if (RegQueryValueExW(hKey, L"Path", nullptr, nullptr, reinterpret_cast<LPBYTE>(g_OriginDotnetPath), &path_size) != ERROR_SUCCESS)
    {
        return;
    }

    wchar_t version_buffer[64];
    DWORD version_size = sizeof(version_buffer);
    if (RegQueryValueExW(hKey, L"Version", nullptr, nullptr, reinterpret_cast<LPBYTE>(version_buffer), &version_size) == ERROR_SUCCESS)
    {
        swprintf(g_OriginHostfxrPath, MAX_PATH, L"%shost\\fxr\\%s\\hostfxr.dll", g_OriginDotnetPath, version_buffer);
    }

    g_OriginHostfxrModule = LoadLibraryW(g_OriginHostfxrPath);

    RegCloseKey(hKey);
}

void setup_modified_app_path()
{
    // TODO: Move to ini file
    // This needs to have a x.runtimeconfig.json file in the same directory as the DLL.
    const wchar_t ReplacedAssemblyName[] = L"MonkeyLoaderWrapper.dll";
    
    wchar_t current_module_path[HOSTFXR_MAX_PATH];
    if (GetModuleFileNameW(nullptr, current_module_path, HOSTFXR_MAX_PATH) == 0)
    {
        return;
    }
    
    std::wstring current_module(current_module_path);
    size_t last_slash_pos = current_module.find_last_of(L"\\/");
    if (last_slash_pos != std::wstring::npos)
    {
        current_module.replace(last_slash_pos + 1, current_module.length() - last_slash_pos - 1, ReplacedAssemblyName);
        wcscpy_s(g_ModifiedAppPath, HOSTFXR_MAX_PATH, current_module.c_str());
    }
    else
    {
        wcscpy_s(g_ModifiedAppPath, HOSTFXR_MAX_PATH, ReplacedAssemblyName);
    }
}
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call != DLL_PROCESS_ATTACH)
        return TRUE;
    
    find_global_dotnet();
    setup_modified_app_path();
    return TRUE;
}

typedef wchar_t char_t;

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

#define SHARED_API extern "C" __declspec(dllexport)
#define HOSTFXR_CALLTYPE __cdecl

typedef int32_t(HOSTFXR_CALLTYPE* hostfxr_main_fn)(const int argc, const char_t** argv);
typedef int32_t(HOSTFXR_CALLTYPE* hostfxr_main_startupinfo_fn)(
    const int argc,
    const char_t** argv,
    const char_t* host_path,
    const char_t* dotnet_root,
    const char_t* app_path);
typedef int32_t(HOSTFXR_CALLTYPE* hostfxr_main_bundle_startupinfo_fn)(
    const int argc,
    const char_t** argv,
    const char_t* host_path,
    const char_t* dotnet_root,
    const char_t* app_path,
    int64_t bundle_header_offset);

SHARED_API int HOSTFXR_CALLTYPE hostfxr_main_bundle_startupinfo(const int argc, const char_t* argv[], const char_t* host_path, const char_t* dotnet_root, const char_t* app_path, int64_t bundle_header_offset)
{
    if (!g_OriginHostfxrModule)
    {
        return FrameworkMissingFailure;
    }
    
    auto hostfxr_main_bundle_startupinfo_ptr = reinterpret_cast<hostfxr_main_bundle_startupinfo_fn>(
        GetProcAddress(g_OriginHostfxrModule, "hostfxr_main_bundle_startupinfo"));

    if (hostfxr_main_bundle_startupinfo_ptr)
    {
        return hostfxr_main_bundle_startupinfo_ptr(argc, argv, host_path, g_OriginDotnetPath, g_ModifiedAppPath, bundle_header_offset);
    }
    return -1;
}

SHARED_API int HOSTFXR_CALLTYPE hostfxr_main_startupinfo(const int argc, const char_t* argv[], const char_t* host_path, const char_t* dotnet_root, const char_t* app_path)
{
    if (!g_OriginHostfxrModule)
    {
        return FrameworkMissingFailure;
    }
    
    auto hostfxr_main_startupinfo_ptr = reinterpret_cast<hostfxr_main_startupinfo_fn>(
        GetProcAddress(g_OriginHostfxrModule, "hostfxr_main_startupinfo"));

    if (hostfxr_main_startupinfo_ptr)
    {
        return hostfxr_main_startupinfo_ptr(argc, argv, host_path, g_OriginDotnetPath, g_ModifiedAppPath);
    }
    return -1;
}

SHARED_API int HOSTFXR_CALLTYPE hostfxr_main(const int argc, const char_t* argv[])
{
    if (!g_OriginHostfxrModule)
    {
        return FrameworkMissingFailure;
    }
    
    auto hostfxr_main_ptr = reinterpret_cast<hostfxr_main_fn>(
        GetProcAddress(g_OriginHostfxrModule, "hostfxr_main"));

    if (hostfxr_main_ptr)
    {
        return hostfxr_main_ptr(argc, argv);
    }
    return -1;
}

#ifdef __cplusplus
}
#endif // __cplusplus
