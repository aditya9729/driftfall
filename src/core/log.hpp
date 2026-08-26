// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <format>
#include <string_view>

namespace df::log {

enum class Level { Trace, Debug, Info, Warn, Error };

void set_level(Level level);

Level level();

void write(Level level, std::string_view message);

template <typename... Args>
void trace(std::format_string<Args...> fmt, Args&&... args) {
    if (level() <= Level::Trace) write(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (level() <= Level::Debug) write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    if (level() <= Level::Info) write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (level() <= Level::Warn) write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    if (level() <= Level::Error) write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace df::log
