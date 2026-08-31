// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"

namespace df {

/// What the platform thinks this process is costing in memory.
struct MemoryUsage {
    /// The number the operating system charges us for, and therefore the one
    /// that decides whether iOS jetsams the app. On Apple that is the phys
    /// footprint, on Linux the resident set, on the web the size of the
    /// WebAssembly linear memory.
    u64 resident_bytes = 0;

    /// Allocator arena size where the platform can report it separately, 0
    /// otherwise. Only meaningful next to resident_bytes.
    u64 heap_bytes = 0;

    /// False when this platform has no cheap way to answer, in which case the
    /// HUD says so rather than displaying a confident zero.
    bool valid = false;
};

/// Samples current memory use. Cheap but not free — on Linux it opens and
/// parses a procfs entry — so callers should sample at a few hertz rather than
/// once per frame.
[[nodiscard]] MemoryUsage query_memory_usage();

}  // namespace df
