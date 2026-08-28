# PPL (Concurrency::parallel_for) is built into MSVC only — no Linux implementation
if(NOT WIN32)
    message(STATUS "ppl_strategy_benchmark: PPL is MSVC-only, skipped on non-Windows")
    return()
endif()

project(ppl_strategy_benchmark)

file(GLOB_RECURSE SOURCES "src/*.cpp")

include_directories(${CMAKE_SOURCE_DIR}/utils)
add_executable(${PROJECT_NAME} ${SOURCES})

find_package(spdlog REQUIRED)
target_link_libraries(${PROJECT_NAME} spdlog::spdlog)

find_package(benchmark REQUIRED)
target_link_libraries(${PROJECT_NAME} benchmark::benchmark benchmark::benchmark_main)

# StdThreadPool for manual partition comparison — no TBB dependency
# PPL is built into MSVC on Windows (/EHsc required for Concurrency::)
ADD_LIB_MODULE(${PROJECT_NAME} threadPool)
