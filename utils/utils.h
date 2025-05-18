#pragma once

#include <spdlog/spdlog.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <utility>

enum class Status { kSuccess, kFailure };

#define DISABLE_COPY_AND_ASSIGN(ClassName) \
    ClassName(const ClassName&) = delete;  \
    ClassName& operator=(const ClassName&) = delete;

#define DISABLE_CONSTRUCTION(ClassName)   \
    ClassName() = delete;                 \
    ClassName(const ClassName&) = delete; \
    ClassName& operator=(const ClassName&) = delete;

#define DISABLE_MOVE(ClassName)      \
    ClassName(ClassName&&) = delete; \
    ClassName& operator=(ClassName&&) = delete;

#if defined(__clang__)
#define NO_OPTIMIZE __attribute__((optnone))
#elif defined(__GNUC__) || defined(__GNUG__)
#define NO_OPTIMIZE __attribute__((optimize("O0")))
#else
#define NO_OPTIMIZE
#endif

namespace myUtils {
    template <typename Func, typename... Args>
    inline void measure_time_console(Func&& func, Args&&... args) {
        auto start = std::chrono::high_resolution_clock::now();
        std::forward<Func>(func)(std::forward<Args>(args)...);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        double milliseconds = duration.count() / 1e6;
        if (milliseconds < 1000) {
            std::cout << std::scientific << std::setprecision(2) << milliseconds
                      << " ms" << std::endl;
        } else {
            std::cout << std::scientific << std::setprecision(2)
                      << milliseconds / 1000 << " s" << std::endl;
        }
    }

    template <typename Func, typename... Args>
    inline void measure_time(Func&& func, Args&&... args) {
        auto start = std::chrono::high_resolution_clock::now();
        std::forward<Func>(func)(std::forward<Args>(args)...);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        double milliseconds = duration.count() / 1e6;
        if (milliseconds < 1000) {
            spdlog::info("\t{:.2e} ms", milliseconds);  // 使用 spdlog 输出
        } else {
            spdlog::info("\t{:.2e} s",
                         milliseconds / 1000);  // 使用 spdlog 输出
        }
    }

}  // namespace myUtils