project(eight_queen_benchmark)

# Only compile the Google Benchmark sources, NOT the old standalone .cc files
# (which each have their own main() and would conflict).
file(GLOB SOURCES "src/main.cpp" "src/case*.cpp")

include_directories(${CMAKE_SOURCE_DIR}/utils)
include_directories("src")
add_executable(${PROJECT_NAME} ${SOURCES})

find_package(benchmark REQUIRED)
target_link_libraries(${PROJECT_NAME} benchmark::benchmark benchmark::benchmark_main)

find_package(fmt REQUIRED)
target_link_libraries(${PROJECT_NAME} fmt::fmt)

find_package(spdlog REQUIRED)
target_link_libraries(${PROJECT_NAME} spdlog::spdlog)
