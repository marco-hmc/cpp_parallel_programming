#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

extern void nested_test();
extern void mutex_test();
extern void lockCost_test();
extern void sharedData_test();
extern void multiPool_nested_test();

void init() {
    std::string log_file =
        "/home/marco/0_noteWorkspace/3_project/3_cppProject/parallelBenchmark/"
        "log/benchmark.log";
    if (std::filesystem::exists(log_file)) {
        std::filesystem::remove(log_file);
    }
    std::ofstream ofs(log_file, std::ofstream::out | std::ofstream::trunc);
    ofs.close();
    auto file_logger = spdlog::basic_logger_mt("file_logger", log_file);
    spdlog::set_default_logger(file_logger);
    spdlog::set_level(spdlog::level::info);
}

int main() {
    init();
    spdlog::info("Thread count: {}", std::thread::hardware_concurrency());
    spdlog::info("Benchmark started...\n\n");
    nested_test();
    multiPool_nested_test();
    mutex_test();
    lockCost_test();
    sharedData_test();
    spdlog::info("Benchmark completed.");
    return 0;
}