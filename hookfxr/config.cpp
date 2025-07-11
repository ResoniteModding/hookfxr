#include "config.h"

#include "defines.h"
#include "shellapi.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace
{
std::wstring get_dll_directory()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    
    std::wstring exe_path(path);
    size_t last_slash = exe_path.find_last_of(L'\\');
    if (last_slash != std::wstring::npos)
    {
        return exe_path.substr(0, last_slash + 1);
    }
    return L"";
}

std::wstring make_absolute_path(const std::wstring& path)
{
    // If path is already absolute, return as-is
    if (std::filesystem::path(path).is_absolute())
    {
        return path;
    }
    
    std::filesystem::path dll_dir = std::filesystem::path(get_dll_directory()).parent_path();
    std::filesystem::path absolute_path = dll_dir / path;
    
    // Normalize the path (resolve .., ., etc.)
    std::error_code ec;
    std::filesystem::path canonical_path = std::filesystem::canonical(absolute_path, ec);
    
    if (!ec)
    {
        return canonical_path.wstring();
    }
    
    // If canonical fails (file doesn't exist), return the absolute path without resolving
    return std::filesystem::absolute(absolute_path).wstring();
}

std::wstring read_ini_string(const std::wstring& file_path, const std::wstring& section, const std::wstring& key, const std::wstring& default_value = L"")
{
    wchar_t buffer[1024];
    DWORD result = GetPrivateProfileStringW(
        section.c_str(),
        key.c_str(),
        default_value.c_str(),
        buffer,
        sizeof(buffer) / sizeof(wchar_t),
        file_path.c_str()
    );
    
    return std::wstring(buffer);
}

bool read_ini_bool(const std::wstring& file_path, const std::wstring& section, const std::wstring& key, bool default_value = false)
{
    std::wstring value = read_ini_string(file_path, section, key, default_value ? L"true" : L"false");
    std::ranges::transform(value, value.begin(), ::towlower);
    
    return value == L"true" || value == L"1";
}

void parse_command_line(hookfxr_config& config)
{
    int argc;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    if (argv == nullptr)
        return;
    
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg(argv[i]);
        
        if (arg == L"--hookfxr-enable")
        {
            config.m_enable = true;
        }
        else if (arg == L"--hookfxr-disable")
        {
            config.m_enable = false;
        }
        else if (arg == L"--hookfxr-target" && i + 1 < argc)
        {
            config.m_target_assembly = make_absolute_path(argv[++i]);
        }
        else if (arg == L"--hookfxr-dotnet-root" && i + 1 < argc)
        {
            config.m_dotnet_root_override = argv[++i];
        }
    }
    
    LocalFree(argv);
}
}

hookfxr_config get_hookfxr_config()
{
    hookfxr_config config{};

    // Read from hookfxr.ini
    const std::wstring exe_dir = get_dll_directory();
    const std::wstring ini_path = exe_dir + L"hookfxr.ini";
    
    if (GetFileAttributesW(ini_path.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        // Read configuration from ini file
        config.m_enable = read_ini_bool(ini_path, L"hookfxr", L"enable", false);
        config.m_target_assembly = make_absolute_path(read_ini_string(ini_path, L"hookfxr", L"target_assembly"));
        config.m_dotnet_root_override = read_ini_string(ini_path, L"hookfxr", L"dotnet_root_override");
    }
    else
    {
        std::wcerr << L"hookfxr.ini not found in " << exe_dir << L". Using default configuration.\n";
    }

    // Override with command line arguments
    parse_command_line(config);
    
    return config;
}
