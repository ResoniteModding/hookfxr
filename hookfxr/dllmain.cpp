#include "defines.h"

#include <iostream>
#include <string>

#define NETHOST_USE_AS_STATIC
#include <nethost.h>
#include <hostfxr.h>

#include "config.h"

#define HOSTFXR_MAX_PATH 1024

namespace
{
wchar_t g_real_hostfxr_path[HOSTFXR_MAX_PATH] = { '\0' };
wchar_t g_real_dotnet_root_path[HOSTFXR_MAX_PATH] = { '\0' };
HMODULE g_real_hostfxr_module{ nullptr };
    
hookfxr_config g_hookfxr_config;

// src/native/corehost/error_codes.h
enum StatusCode
{
    FrameworkMissingFailure = 0x80008096,
};

bool find_real_dotnet(const wchar_t* app_path)
{
    // Already resolved
    if (g_real_hostfxr_module)
    {
        return true;
    }

    // If we fail here, g_real_hostfxr_module will be nullptr and all the proxy functions will return FrameworkMissingFailure.
    // This causes the apphost to show an error message.
    const get_hostfxr_parameters params = {
        .size = sizeof(get_hostfxr_parameters),
        .assembly_path = app_path,
        .dotnet_root = nullptr
    };
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
        return false;
    }
    
    if (fxr_path_size > 0 && fxr_path_size < HOSTFXR_MAX_PATH &&
        dotnet_root_path_size > 0 && dotnet_root_path_size < HOSTFXR_MAX_PATH)
    {
        g_real_hostfxr_module = LoadLibraryW(g_real_hostfxr_path);
        if (!g_real_hostfxr_module)
        {
            std::cerr << "Failed to load hostfxr module\n";
            return false;
        }
        
        return true;
    }

    std::wcerr << L"Hostfxr path or dotnet root path size is invalid: "
              << fxr_path_size << ", " << dotnet_root_path_size << '\n';
    return false;
}

const wchar_t* get_overriden_app_path(const wchar_t* real_app_path)
{
    if (!g_hookfxr_config.m_enable || g_hookfxr_config.m_target_assembly.empty())
    {
        return real_app_path;
    }

    return g_hookfxr_config.m_target_assembly.c_str();
}
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call != DLL_PROCESS_ATTACH)
        return TRUE;

    g_hookfxr_config = get_hookfxr_config();

    // Write dotnet override to DOTNET_ROOT so that nethost will resolve it from there
    if (!g_hookfxr_config.m_dotnet_root_override.empty())
    {
        SetEnvironmentVariableW(L"DOTNET_ROOT", g_hookfxr_config.m_dotnet_root_override.c_str());
    }
    
    return TRUE;
}

typedef wchar_t char_t;

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

#define SHARED_API extern "C" __declspec(dllexport)

SHARED_API int HOSTFXR_CALLTYPE hostfxr_main_bundle_startupinfo(const int argc, const char_t* argv[], const char_t* host_path, const char_t* dotnet_root, const char_t* app_path, int64_t bundle_header_offset)
{
    const wchar_t* applicable_app_path = get_overriden_app_path(app_path);
    if (!find_real_dotnet(applicable_app_path))
    {
        return FrameworkMissingFailure;
    }
    
    auto hostfxr_main_bundle_startupinfo_ptr = reinterpret_cast<hostfxr_main_bundle_startupinfo_fn>(
        GetProcAddress(g_real_hostfxr_module, "hostfxr_main_bundle_startupinfo"));

    if (hostfxr_main_bundle_startupinfo_ptr)
    {
        return hostfxr_main_bundle_startupinfo_ptr(
            argc,
            argv,
            host_path,
            g_real_dotnet_root_path,
            applicable_app_path,
            bundle_header_offset);
    }
    return -1;
}

SHARED_API int HOSTFXR_CALLTYPE hostfxr_main_startupinfo(const int argc, const char_t* argv[], const char_t* host_path, const char_t* dotnet_root, const char_t* app_path)
{
    const wchar_t* applicable_app_path = get_overriden_app_path(app_path);
    if (!find_real_dotnet(applicable_app_path))
    {
        return FrameworkMissingFailure;
    }
    
    auto hostfxr_main_startupinfo_ptr = reinterpret_cast<hostfxr_main_startupinfo_fn>(
        GetProcAddress(g_real_hostfxr_module, "hostfxr_main_startupinfo"));

    if (hostfxr_main_startupinfo_ptr)
    {
        return hostfxr_main_startupinfo_ptr(
            argc,
            argv,
            host_path,
            g_real_dotnet_root_path,
            applicable_app_path);
    }
    return -1;
}

SHARED_API int HOSTFXR_CALLTYPE hostfxr_main(const int argc, const char_t* argv[])
{
    // Unsupported
    return -1;
}

#ifdef __cplusplus
}
#endif // __cplusplus
