#include "defines.h"
#include "config.h"

#include <iostream>
#include <string>
#include <filesystem>

#include <safetyhook.hpp>

#define NETHOST_USE_AS_STATIC
#include <nethost/nethost.h>
#include <hostfxr.h>
#include <host_interface.h>

#define HOSTFXR_MAX_PATH 1024

namespace
{
wchar_t g_real_hostfxr_path[HOSTFXR_MAX_PATH] = { '\0' };
wchar_t g_real_dotnet_root_path[HOSTFXR_MAX_PATH] = { '\0' };
wchar_t g_original_app_path[HOSTFXR_MAX_PATH] = { '\0' };
wchar_t g_additional_deps_buffer[HOSTFXR_MAX_PATH] = { '\0' };
HMODULE g_real_hostfxr_module{ nullptr };

safetyhook::InlineHook g_inline_hook_loadlibraryex;
safetyhook::InlineHook g_inline_hook_corehost_load;
    
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
        HFXR_WERROR << "Failed to get hostfxr path with root: " << std::hex << ret << '\n';
        return false;
    }
    
    if (fxr_path_size > 0 && fxr_path_size < HOSTFXR_MAX_PATH &&
        dotnet_root_path_size > 0 && dotnet_root_path_size < HOSTFXR_MAX_PATH)
    {
        g_real_hostfxr_module = LoadLibraryW(g_real_hostfxr_path);
        if (!g_real_hostfxr_module)
        {
            HFXR_ERROR << "Failed to load hostfxr module\n";
            return false;
        }

        return true;
    }

    HFXR_WERROR << L"hostfxr path or dotnet root path size is invalid: "
              << fxr_path_size << ", " << dotnet_root_path_size << '\n';
    return false;
}

const wchar_t* get_overriden_app_path(const wchar_t* real_app_path)
{
    wcscpy_s(g_original_app_path, HOSTFXR_MAX_PATH, real_app_path);
    
    if (!g_hookfxr_config.m_enable || g_hookfxr_config.m_target_assembly.empty())
    {
        return real_app_path;
    }

    return g_hookfxr_config.m_target_assembly.c_str();
}

int corehost_load_detour(host_interface_t* init)
{
    // Check if there are any breaking changes in the host interface
    if (init->version_hi != HOST_INTERFACE_LAYOUT_VERSION_HI)
    {
        // If this actually ever happens, we need to be more clever here and be aware of multiple
        // host interface versions. For now, we just crash.
        HFXR_UNREACHABLE("Host interface version mismatch");
    }

    // Find .deps.json from g_original_app_path, replace .dll extension
    std::wstring deps_path(g_original_app_path);
    if (const size_t dot_pos = deps_path.rfind(L'.'); dot_pos != std::wstring::npos)
    {
        deps_path.replace(dot_pos, deps_path.size() - dot_pos, L".deps.json");
    }
    else
    {
        deps_path += L".deps.json";
    }

    wcscpy_s(g_additional_deps_buffer, HOSTFXR_MAX_PATH, deps_path.c_str());

    if (std::filesystem::exists(deps_path))
    {
        init->additional_deps_serialized = g_additional_deps_buffer;
    }
    else
    {
        HFXR_WERROR << L".deps.json not found at " << deps_path << L".\n";
    }

    return g_inline_hook_corehost_load.call<int>(init);
}

HMODULE loadlibrary_detour(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    if (const HMODULE mod = g_inline_hook_loadlibraryex.call<HMODULE>(lpLibFileName, hFile, dwFlags))
    {
        // Check if we're loading hostpolicy
        if (const std::wstring in_path(lpLibFileName); !in_path.ends_with(L"hostpolicy.dll"))
        {
            return mod;
        }

        if (g_inline_hook_corehost_load)
        {
            return mod;
        }

        // Disable this hook, as we only want to hook hostpolicy.dll once and don't care about anything else
        if (!g_inline_hook_loadlibraryex.disable().has_value())
        {
            HFXR_UNREACHABLE("Could not disable loadlibrary hook");
        }

        void* corehost_load_fn =
            reinterpret_cast<void*>(GetProcAddress(mod, "corehost_load"));

        if (!corehost_load_fn)
        {
            HFXR_UNREACHABLE("Could not find corehost_load in hostpolicy.dll");
        }

        g_inline_hook_corehost_load = safetyhook::create_inline(
            corehost_load_fn,
            &corehost_load_detour);
        
        return mod;
    }

    return NULL;
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

    if (g_hookfxr_config.m_merge_deps_json)
    {
        // Hook LoadLibraryExW to intercept hostpolicy.dll loading, as we need to hook one of its exports (corehost_load)
        // before it is called by the apphost.
        g_inline_hook_loadlibraryex = safetyhook::create_inline(
            LoadLibraryExW,
            loadlibrary_detour);
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
