#pragma once
#include <string>

struct hookfxr_config
{
    bool m_enable{ false };
    std::wstring m_target_assembly;
    std::wstring m_dotnet_root_override;
};

hookfxr_config get_hookfxr_config();
