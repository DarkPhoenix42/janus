#pragma once

#include <cstddef>
#include <new>

#ifdef __cpp_lib_hardware_interference_size
constexpr size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
constexpr size_t kCacheLineSize = 64;
#endif
