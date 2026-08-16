cmake_minimum_required(VERSION 3.16)
project(CoroutineTutorial LANGUAGES CXX)

# C++20 标准（协程是 C++20 特性）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 中文注释/字符串：MSVC 使用 /utf-8 防止源码按本地代码页 (GBK) 误读
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ============================================================
# 1_cpp20 - C++20 协程（主教学路径，编译运行）
# ============================================================
add_executable(01_promiseType
    1_cpp20/1_promiseType.cpp)
add_executable(02_awaiter
    1_cpp20/2_awaiter.cpp)
add_executable(03_customAwaiter
    1_cpp20/3_customAwaiter.cpp)
add_executable(04_generator
    1_cpp20/4_generator.cpp)
add_executable(05_task
    1_cpp20/5_task.cpp)
add_executable(06_scheduler
    1_cpp20/6_scheduler.cpp)
add_executable(07_quiz
    1_cpp20/7_quiz.cpp)

# ============================================================
# 2_boost - Boost.Coroutine2 参考（默认不编译）
# 本机未安装 Boost，2_boost/*.cpp 仅供阅读对照。
# 安装 Boost 后自动启用。各文件头部也附有手动编译命令。
# ============================================================
find_package(Boost QUIET)
if(Boost_FOUND)
    add_executable(08_boost_asymmetric
        2_boost/1_asymmetric.cpp)
    target_link_libraries(08_boost_asymmetric PRIVATE Boost::coroutine)

    add_executable(09_boost_symmetric
        2_boost/2_symmetric.cpp)
    target_link_libraries(09_boost_symmetric PRIVATE Boost::coroutine)

    add_executable(10_boost_bidirectional
        2_boost/3_bidirectional.cpp)
    target_link_libraries(10_boost_bidirectional PRIVATE Boost::coroutine)
endif()

# ============================================================
# 3_impl - 从零实现协程（纯 C++，无任何依赖，编译运行）
# ============================================================
add_executable(11_manualStateMachine
    3_impl/1_manualStateMachine.cpp)
add_executable(12_macroCoroutine
    3_impl/2_macroCoroutine.cpp)
add_executable(13_simpleScheduler
    3_impl/3_simpleScheduler.cpp)
