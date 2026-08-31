// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "core/mem_stats.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten/heap.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task_info.h>
#elif defined(__linux__)
#include <cstdio>
#include <unistd.h>
#endif

namespace df {

MemoryUsage query_memory_usage() {
    MemoryUsage usage;

#if defined(__EMSCRIPTEN__)
    // The wasm linear memory *is* the process footprint as far as the browser
    // is concerned; there is no separate resident set to ask about.
    const u64 heap = static_cast<u64>(emscripten_get_heap_size());
    usage.heap_bytes = heap;
    usage.resident_bytes = heap;
    usage.valid = true;

#elif defined(__APPLE__)
    // phys_footprint, not resident_size: the footprint is what jetsam actually
    // measures against, and the two diverge by hundreds of megabytes on a
    // device under memory pressure.
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    const kern_return_t result =
        task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count);
    if (result == KERN_SUCCESS) {
        usage.resident_bytes = static_cast<u64>(info.phys_footprint);
        usage.valid = true;
    }

#elif defined(__linux__)
    // /proc/self/statm reports pages: size resident shared text lib data dt.
    if (std::FILE* statm = std::fopen("/proc/self/statm", "r"); statm != nullptr) {
        unsigned long total_pages = 0;
        unsigned long resident_pages = 0;
        if (std::fscanf(statm, "%lu %lu", &total_pages, &resident_pages) == 2) {
            const long page_size = ::sysconf(_SC_PAGESIZE);
            if (page_size > 0) {
                usage.resident_bytes = static_cast<u64>(resident_pages) * static_cast<u64>(page_size);
                usage.valid = true;
            }
        }
        std::fclose(statm);
    }
#endif

    return usage;
}

}  // namespace df
