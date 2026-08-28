project(tbb_benchmark)


file(GLOB_RECURSE SOURCES "src/*.cpp")

include_directories("include")
include_directories(${CMAKE_SOURCE_DIR}/utils)
add_executable(${PROJECT_NAME} ${SOURCES})

find_package(spdlog REQUIRED)
target_link_libraries(${PROJECT_NAME} spdlog::spdlog)

find_package(benchmark REQUIRED)
target_link_libraries(${PROJECT_NAME} benchmark::benchmark benchmark::benchmark_main)

# 显式声明 TBB 依赖（旧版靠 tbbThreadPool 死依赖传递引入）
find_package(TBB CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} TBB::tbb)

# tbb::mutex 在 oneTBB 2021 是 PREVIEW 特性（spin+wait 混合实现），需显式开启；
# 报告需注明：旧版声称测的「tbb::mutex」从未真正存在/被测过
target_compile_definitions(${PROJECT_NAME} PRIVATE __TBB_PREVIEW_MUTEXES)

ADD_LIB_MODULE(${PROJECT_NAME} threadPool)