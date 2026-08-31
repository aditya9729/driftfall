// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "core/mem_stats.hpp"
#include "core/perf_budget.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace df;

TEST_CASE("memory can be sampled without lying about it") {
    const MemoryUsage usage = query_memory_usage();

    if (!usage.valid) {
        // A platform with no cheap way to answer must say so rather than
        // report a confident zero — the HUD renders "unavailable" for this.
        CHECK(usage.resident_bytes == 0);
        CHECK(usage.heap_bytes == 0);
        return;
    }

    // A running process with a test binary loaded is never using nothing, and
    // is never using a terabyte either. Both bounds catch a unit mix-up
    // (pages reported as bytes, or the reverse).
    CHECK(usage.resident_bytes > 1024ull * 1024ull);
    CHECK(usage.resident_bytes < 1024ull * 1024ull * 1024ull * 1024ull);
}

TEST_CASE("sampling is stable across repeated calls") {
    const MemoryUsage first = query_memory_usage();
    const MemoryUsage second = query_memory_usage();

    CHECK(first.valid == second.valid);
    if (!first.valid) return;

    // Nothing allocated in between, so the figure should not lurch. A tenfold
    // swing would mean we are reading the wrong field.
    CHECK(second.resident_bytes > first.resident_bytes / 10);
    CHECK(second.resident_bytes < first.resident_bytes * 10);
}

TEST_CASE("resident memory tracks memory actually touched") {
    const MemoryUsage before = query_memory_usage();
    if (!before.valid) return;

    // Touch every page: reserving address space is not the same as costing
    // memory, and the number the HUD shows has to be the one that costs.
    constexpr usize kPageSize = 4096;
    constexpr usize kBytes = 64ull * 1024ull * 1024ull;
    std::vector<unsigned char> block(kBytes, 0);
    for (usize i = 0; i < kBytes; i += kPageSize) block[i] = static_cast<unsigned char>(i);

    const MemoryUsage after = query_memory_usage();
    REQUIRE(after.valid);

    // Well under the 64 MB touched, so that a sampler which rounds or reports
    // a slightly different flavour of "resident" still passes.
    CHECK(after.resident_bytes > before.resident_bytes + 8ull * 1024ull * 1024ull);

    // Keep the block observably alive past the second sample.
    CHECK(block[kPageSize] == static_cast<unsigned char>(kPageSize));
}

TEST_CASE("a sector fits inside the memory budget with room for the game") {
    // The architecture doc's whole case for bounded sectors is that one fits
    // in a phone's budget. If a bare process were already close to the jetsam
    // ceiling, that case would be gone.
    const MemoryUsage usage = query_memory_usage();
    if (!usage.valid) return;
    CHECK(usage.resident_bytes < budget::kResidentBytes);
}
