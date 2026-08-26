// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "core/job_system.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace df {

namespace {

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
constexpr bool kThreadsAvailable = false;
#else
constexpr bool kThreadsAvailable = true;
#endif

unsigned default_worker_count() {
    if constexpr (!kThreadsAvailable) return 0;
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw <= 1) return 0;
    // Leave the main thread and one core of headroom. Saturating every core on
    // a phone is how you get thermal throttling at minute twelve, which costs
    // far more frame time than the extra worker ever bought.
    return std::clamp(hw - 2u, 1u, 3u);
}

}  // namespace

struct JobSystem::Impl {
    std::vector<std::thread> workers;
    std::deque<Job> queue;
    std::mutex mutex;
    std::condition_variable work_ready;
    std::condition_variable all_done;
    usize in_flight = 0;
    bool stopping = false;
};

JobSystem::JobSystem(unsigned worker_count) : impl_(std::make_unique<Impl>()) {
    const unsigned count = worker_count == 0 ? default_worker_count() : worker_count;
    if (count == 0) {
        log::info("job system: synchronous (no worker threads)");
        return;
    }

    impl_->workers.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        impl_->workers.emplace_back([this] {
            for (;;) {
                Job job;
                {
                    std::unique_lock lock(impl_->mutex);
                    impl_->work_ready.wait(lock, [this] { return impl_->stopping || !impl_->queue.empty(); });
                    if (impl_->stopping && impl_->queue.empty()) return;
                    job = std::move(impl_->queue.front());
                    impl_->queue.pop_front();
                }
                job();
                {
                    std::scoped_lock lock(impl_->mutex);
                    if (--impl_->in_flight == 0) impl_->all_done.notify_all();
                }
            }
        });
    }
    log::info("job system: {} worker thread(s)", count);
}

JobSystem::~JobSystem() {
    if (impl_->workers.empty()) return;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->work_ready.notify_all();
    for (auto& worker : impl_->workers) {
        if (worker.joinable()) worker.join();
    }
}

void JobSystem::submit(Job job) {
    if (impl_->workers.empty()) {
        job();
        return;
    }
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->queue.push_back(std::move(job));
        ++impl_->in_flight;
    }
    impl_->work_ready.notify_one();
}

void JobSystem::wait_idle() {
    if (impl_->workers.empty()) return;
    std::unique_lock lock(impl_->mutex);
    impl_->all_done.wait(lock, [this] { return impl_->in_flight == 0; });
}

unsigned JobSystem::worker_count() const {
    return static_cast<unsigned>(impl_->workers.size());
}

}  // namespace df
