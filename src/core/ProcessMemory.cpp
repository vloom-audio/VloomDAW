// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/ProcessMemory.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// windows.h first: psapi.h relies on its types.
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

namespace vloomdaw {

std::optional<std::uint64_t> processMemoryBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    // K32GetProcessMemoryInfo lives in kernel32, so no psapi.lib is needed.
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == FALSE)
        return std::nullopt;
    return static_cast<std::uint64_t>(counters.WorkingSetSize);
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count)
        != KERN_SUCCESS)
        return std::nullopt;
    return static_cast<std::uint64_t>(info.resident_size);
#elif defined(__linux__)
    // /proc/self/statm: total pages, then resident pages.
    std::ifstream statm("/proc/self/statm");
    std::uint64_t totalPages = 0;
    std::uint64_t residentPages = 0;
    if (!(statm >> totalPages >> residentPages))
        return std::nullopt;
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0)
        return std::nullopt;
    return residentPages * static_cast<std::uint64_t>(pageSize);
#else
    return std::nullopt;
#endif
}

} // namespace vloomdaw
