#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <iostream>
#include <string>

#define NETHOST_USE_AS_STATIC
#include <nethost.h>

#define HOSTFXR_MAX_PATH 1024

namespace
{
wchar_t g_real_hostfxr_path[HOSTFXR_MAX_PATH] = { '\0' };
wchar_t g_real_dotnet_root_path[HOSTFXR_MAX_PATH] = { '\0' };
wchar_t g_modified_app_path[HOSTFXR_MAX_PATH] = { '\0' };

HMODULE g_real_hostfxr_module{ nullptr };

// src/native/corehost/error_codes.h
enum StatusCode
{
    FrameworkMissingFailure = 0x80008096,
};
    
void find_real_dotnet()
{
    // If we fail here, g_RealHostfxrModule will be nullptr and all the proxy functions will return FrameworkMissingFailure.
    // This causes the apphost to show an error message.
    
    constexpr get_hostfxr_parameters params = { sizeof(get_hostfxr_parameters), g_modified_app_path, nullptr };
    size_t fxr_path_size{ HOSTFXR_MAX_PATH };
    size_t dotnet_root_path_size{ HOSTFXR_MAX_PATH };
    const int ret = get_hostfxr_path_with_root(
        g_real_hostfxr_path,
        &fxr_path_size,
        g_real_dotnet_root_path,
        &dotnet_root_path_size,
        &params);

    if (ret != 0)
    {
        std::cerr << "Failed to get hostfxr path with root: " << std::hex << ret << '\n';
        return;
    }
    
    if (fxr_path_size > 0 && fxr_path_size < HOSTFXR_MAX_PATH &&
        dotnet_root_path_size > 0 && dotnet_root_path_size < HOSTFXR_MAX_PATH)
    {
        g_real_hostfxr_module = LoadLibraryW(g_real_hostfxr_path);
        if (!g_real_hostfxr_module)
        {
            std::cerr << "Failed to load hostfxr module: " << g_real_hostfxr_path << '\n';
        }
        
        return;
    }

    std::cerr << "Hostfxr path or dotnet root path size is invalid: "
              << fxr_path_size << ", " << dotnet_root_path_size << '\n';
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
        wcscpy_s(g_modified_app_path, HOSTFXR_MAX_PATH, current_module.c_str());
    }
    else
    {
        wcscpy_s(g_modified_app_path, HOSTFXR_MAX_PATH, ReplacedAssemblyName);
    }
}
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call != DLL_PROCESS_ATTACH)
        return TRUE;

    setup_modified_app_path();
    find_real_dotnet();
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
    if (!g_real_hostfxr_module)
    {
        return FrameworkMissingFailure;
    }
    
    auto hostfxr_main_bundle_startupinfo_ptr = reinterpret_cast<hostfxr_main_bundle_startupinfo_fn>(
        GetProcAddress(g_real_hostfxr_module, "hostfxr_main_bundle_startupinfo"));

    if (hostfxr_main_bundle_startupinfo_ptr)
    {
        return hostfxr_main_bundle_startupinfo_ptr(argc, argv, host_path, g_real_dotnet_root_path, g_modified_app_path, bundle_header_offset);
    }
    return -1;
}

SHARED_API int HOSTFXR_CALLTYPE hostfxr_main_startupinfo(const int argc, const char_t* argv[], const char_t* host_path, const char_t* dotnet_root, const char_t* app_path)
{
    if (!g_real_hostfxr_module)
    {
        return FrameworkMissingFailure;
    }
    
    auto hostfxr_main_startupinfo_ptr = reinterpret_cast<hostfxr_main_startupinfo_fn>(
        GetProcAddress(g_real_hostfxr_module, "hostfxr_main_startupinfo"));

    if (hostfxr_main_startupinfo_ptr)
    {
        return hostfxr_main_startupinfo_ptr(argc, argv, host_path, g_real_dotnet_root_path, g_modified_app_path);
    }
    return -1;
}

SHARED_API int HOSTFXR_CALLTYPE hostfxr_main(const int argc, const char_t* argv[])
{
    if (!g_real_hostfxr_module)
    {
        return FrameworkMissingFailure;
    }
    
    auto hostfxr_main_ptr = reinterpret_cast<hostfxr_main_fn>(
        GetProcAddress(g_real_hostfxr_module, "hostfxr_main"));

    if (hostfxr_main_ptr)
    {
        return hostfxr_main_ptr(argc, argv);
    }
    return -1;
}

#ifdef __cplusplus
}
#endif // __cplusplus
