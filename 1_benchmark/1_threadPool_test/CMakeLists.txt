project(pool_benchmark)

file(GLOB_RECURSE SOURCES "src/*.cpp")

include_directories(${CMAKE_SOURCE_DIR}/utils)
add_executable(${PROJECT_NAME} ${SOURCES})

find_package(benchmark REQUIRED)
target_link_libraries(${PROJECT_NAME} benchmark::benchmark benchmark::benchmark_main)

find_package(fmt REQUIRED)
target_link_libraries(${PROJECT_NAME} fmt::fmt)
ADD_LIB_MODULE(${PROJECT_NAME} threadPool)