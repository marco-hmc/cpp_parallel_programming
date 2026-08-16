project(tbb_benchmark)


file(GLOB_RECURSE SOURCES "src/*.cpp")

include_directories("include")
include_directories(${CMAKE_SOURCE_DIR}/utils)
add_executable(${PROJECT_NAME} ${SOURCES})

find_package(spdlog REQUIRED)
target_link_libraries(${PROJECT_NAME} spdlog::spdlog)

find_package(benchmark REQUIRED)
target_link_libraries(${PROJECT_NAME} benchmark::benchmark benchmark::benchmark_main)

ADD_LIB_MODULE(${PROJECT_NAME} tbbThreadPool threadPool)