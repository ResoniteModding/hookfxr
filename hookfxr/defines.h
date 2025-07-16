#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <iostream>

#define HFXR_UNREACHABLE(msg) \
    do { \
        std::cerr << __FILE__ << ":" << __LINE__ << " hookfxr critical:" << (msg) << std::endl; \
        abort(); \
    } while(0)

#define HFXR_ERROR std::cerr << __FILE__ << ":" << __LINE__ << " hookfxr error:"
#define HFXR_WERROR std::wcerr << __FILE__ << ":" << __LINE__ << " hookfxr error:"
