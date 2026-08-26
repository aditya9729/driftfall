// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "core/log.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace df::log {
namespace {

std::atomic<Level> g_level{Level::Info};
std::mutex g_mutex;

constexpr const char* tag(Level level) {
    switch (level) {
        case Level::Trace:
            return "TRACE";
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO ";
        case Level::Warn:
            return "WARN ";
        case Level::Error:
            return "ERROR";
    }
    return "?????";
}

}  // namespace

void set_level(Level level) {
    g_level.store(level, std::memory_order_relaxed);
}

Level level() {
    return g_level.load(std::memory_order_relaxed);
}

void write(Level lvl, std::string_view message) {
    // Worker threads mesh chunks and log from off the main thread, so the
    // write itself has to be atomic or lines interleave mid-word.
    std::scoped_lock lock(g_mutex);
    std::FILE* out = lvl >= Level::Warn ? stderr : stdout;
    std::fprintf(out, "[df %s] %.*s\n", tag(lvl), static_cast<int>(message.size()), message.data());
    if (lvl >= Level::Warn) std::fflush(out);
}

}  // namespace df::log
