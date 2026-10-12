#pragma once

#include <chrono>
#include <ctime>

#include "perf_utils.h"

namespace Common {
inline auto& getCurrentTimeStr(std::string* time_str) {
    const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    time_str->assign(ctime(&time));
    if (!time_str->empty()) time_str->at(time_str->length() - 1) = '\0';
    return *time_str;
}
}  // namespace Common
