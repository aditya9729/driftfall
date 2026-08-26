// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"

#include <functional>
#include <memory>

namespace df {

/// A small work-stealing-free thread pool.
///
/// Its one real customer is chunk remeshing: when the player blows a hole in a
/// bulkhead, up to four chunks go dirty at once and each needs a greedy-mesh
/// pass that costs ~0.4 ms. Doing that on the main thread blows the 16.6 ms
/// budget outright, so meshing happens here and finished meshes are picked up
/// by the renderer on a later frame.
///
/// On Emscripten without -pthread this degrades to synchronous execution, so
/// the calling code never needs a separate path.
class JobSystem {
public:
    using Job = std::function<void()>;

    /// worker_count == 0 picks a sensible default for the device.
    explicit JobSystem(unsigned worker_count = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void submit(Job job);

    /// Blocks until every submitted job has finished. Used at level load and
    /// in tests; never call it from a frame.
    void wait_idle();

    [[nodiscard]] unsigned worker_count() const;

    /// True when jobs run inline on the calling thread (no worker threads).
    [[nodiscard]] bool is_synchronous() const { return worker_count() == 0; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace df
